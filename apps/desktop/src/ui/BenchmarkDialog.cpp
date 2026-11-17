#include "BenchmarkDialog.h"
#include "OperationController.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
#include <vector>

namespace m7arc::ui {
namespace {
class BenchmarkDialog final : public QDialog {
  public:
    BenchmarkDialog(QWidget* parent, OperationController* operations)
        : QDialog(parent), operations_(operations) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QObject::tr("7-Zip 性能测试"));
        resize(790, 600);
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        dictionary_ = new QComboBox(this);
        for (const auto mib : {1, 2, 4, 8, 16, 32, 64, 128, 256})
            dictionary_->addItem(QString::number(mib) + " MiB", quint64(mib) << 20);
        dictionary_->setCurrentIndex(4);
        threads_ = new QSpinBox(this);
        threads_->setRange(1, qMax(1, QThread::idealThreadCount()));
        threads_->setValue(qMin(2, threads_->maximum()));
        passes_ = new QSpinBox(this);
        passes_->setRange(1, 100);
        passes_->setValue(3);
        seconds_ = new QSpinBox(this);
        seconds_->setRange(1, 60);
        seconds_->setValue(2);
        form->addRow(QObject::tr("字典大小"), dictionary_);
        form->addRow(QObject::tr("线程数"), threads_);
        form->addRow(QObject::tr("轮数"), passes_);
        form->addRow(QObject::tr("每轮目标秒数"), seconds_);
        layout->addLayout(form);
        status_ = new QLabel(QObject::tr("准备就绪"), this);
        layout->addWidget(status_);
        results_ = new QTableWidget(2, 6, this);
        results_->setVerticalHeaderLabels({QObject::tr("压缩"), QObject::tr("解压")});
        results_->setHorizontalHeaderLabels({QObject::tr("当前速度"), QObject::tr("平均速度"),
                                             QObject::tr("当前评分"), QObject::tr("平均评分"),
                                             QObject::tr("当前 CPU"), QObject::tr("平均 CPU")});
        results_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 6; ++column)
                results_->setItem(row, column, new QTableWidgetItem("—"));
        results_->setMaximumHeight(130);
        layout->addWidget(results_);
        log_ = new QPlainTextEdit(this);
        log_->setReadOnly(true);
        layout->addWidget(log_);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        start_ = buttons->addButton(QObject::tr("开始 / 重新开始"), QDialogButtonBox::ActionRole);
        stop_ = buttons->addButton(QObject::tr("停止"), QDialogButtonBox::ActionRole);
        stop_->setEnabled(false);
        auto* save = buttons->addButton(QObject::tr("保存结果…"), QDialogButtonBox::ActionRole);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::close);
        connect(start_, &QPushButton::clicked, this, [this] {
            if (operations_->busy()) {
                operations_->showProgress();
                return;
            }
            measurements_.clear();
            log_->clear();
            stopped_ = false;
            options_.threads = threads_->value();
            options_.dictionaryBytes = dictionary_->currentData().toULongLong();
            options_.seconds = seconds_->value();
            remaining_ = passes_->value();
            setRunning(true);
            nextPass();
        });
        connect(stop_, &QPushButton::clicked, this, [this] {
            stopped_ = true;
            operations_->cancel();
        });
        connect(save, &QPushButton::clicked, this, [this] {
            if (log_->toPlainText().isEmpty())
                return;
            const auto path =
                QFileDialog::getSaveFileName(this, QObject::tr("保存性能测试结果"), "M7Arc-benchmark.txt");
            if (path.isEmpty())
                return;
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                const auto bytes = log_->toPlainText().toUtf8();
                if (file.write(bytes) == bytes.size() && file.commit())
                    status_->setText(QObject::tr("结果已保存"));
                else
                    status_->setText(file.errorString());
            } else
                status_->setText(file.errorString());
        });
    }

  protected:
    void closeEvent(QCloseEvent* event) override {
        if (running_) {
            closeRequested_ = true;
            stopped_ = true;
            operations_->cancel();
            event->ignore();
        } else
            QDialog::closeEvent(event);
    }

  private:
    void setRunning(bool running) {
        running_ = running;
        start_->setEnabled(!running);
        stop_->setEnabled(running);
        dictionary_->setEnabled(!running);
        threads_->setEnabled(!running);
        passes_->setEnabled(!running);
        seconds_->setEnabled(!running);
    }
    void nextPass() {
        if (stopped_ || remaining_ <= 0) {
            setRunning(false);
            status_->setText(stopped_ ? QObject::tr("测试已停止") : QObject::tr("测试完成"));
            if (closeRequested_)
                close();
            return;
        }
        status_->setText(
            QObject::tr("正在执行第 %1 / %2 轮").arg(measurements_.size() + 1).arg(passes_->value()));
        auto measurement = std::make_shared<BenchmarkResult>();
        const QPointer<BenchmarkDialog> guard(this);
        operations_->run(
            QObject::tr("7-Zip 性能测试"),
            [options = options_, measurement](ArchiveEngine& engine, const Callbacks& callbacks) {
                *measurement = engine.benchmark(options, callbacks);
                OperationResult result;
                result.cancelled = callbacks.cancelled && callbacks.cancelled();
                return result;
            },
            [guard, measurement](const OperationResult& result) {
                if (!guard)
                    return;
                if (!result.ok())
                    guard->stopped_ = true;
                else {
                    guard->measurements_.push_back(*measurement);
                    guard->updateResults();
                }
                --guard->remaining_;
                QTimer::singleShot(0, guard, [guard] {
                    if (guard)
                        guard->nextPass();
                });
            });
    }
    void updateResults() {
        const auto& current = measurements_.back();
        quint64 compression = 0, decompression = 0, encodeRating = 0, decodeRating = 0;
        quint64 encodeCpu = 0, decodeCpu = 0;
        for (const auto& measurement : measurements_) {
            compression += measurement.encodeBytesPerSecond;
            decompression += measurement.decodeBytesPerSecond;
            encodeRating += measurement.encodeRating;
            decodeRating += measurement.decodeRating;
            encodeCpu += measurement.encodeCpuPercent;
            decodeCpu += measurement.decodeCpuPercent;
        }
        auto speed = [](quint64 bytes) { return QLocale().formattedDataSize(bytes) + " / s"; };
        auto rating = [](quint64 value) {
            return QString::number(double(value) / 1000000, 'f', 0) + " MIPS";
        };
        auto cpu = [](quint64 value) { return QString::number(value) + "%"; };
        const auto count = measurements_.size();
        const QList<QStringList> values{
            {speed(current.encodeBytesPerSecond), speed(compression / count), rating(current.encodeRating),
             rating(encodeRating / count), cpu(current.encodeCpuPercent), cpu(encodeCpu / count)},
            {speed(current.decodeBytesPerSecond), speed(decompression / count), rating(current.decodeRating),
             rating(decodeRating / count), cpu(current.decodeCpuPercent), cpu(decodeCpu / count)}};
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 6; ++column)
                results_->item(row, column)->setText(values[row][column]);
        log_->appendPlainText(QObject::tr("轮次 %1 · LZMA · %2 · %3 线程\n压缩 %4 / %5；解压 %6 / "
                                          "%7\nCPU：压缩 %8；解压 %9（100% = 一个核心）")
                                  .arg(count)
                                  .arg(QLocale().formattedDataSize(options_.dictionaryBytes))
                                  .arg(options_.threads)
                                  .arg(speed(current.encodeBytesPerSecond), rating(current.encodeRating),
                                       speed(current.decodeBytesPerSecond), rating(current.decodeRating))
                                  .arg(cpu(current.encodeCpuPercent), cpu(current.decodeCpuPercent)));
    }
    OperationController* operations_;
    QComboBox* dictionary_;
    QSpinBox *threads_, *passes_, *seconds_;
    QLabel* status_;
    QTableWidget* results_;
    QPlainTextEdit* log_;
    QPushButton *start_, *stop_;
    BenchmarkOptions options_;
    std::vector<BenchmarkResult> measurements_;
    int remaining_ = 0;
    bool running_ = false, stopped_ = false, closeRequested_ = false;
};
} // namespace
void showBenchmark(QWidget* parent, OperationController* operations) {
    if (operations->busy()) {
        operations->showProgress();
        return;
    }
    auto* dialog = new BenchmarkDialog(parent, operations);
    dialog->show();
}
} // namespace m7arc::ui
