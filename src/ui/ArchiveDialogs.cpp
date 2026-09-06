#include "ArchiveDialogs.h"
#include "app/FormatCatalog.h"
#include "platform/services/MacServices.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
namespace m7arc::ui {
namespace {
QWidget* pathField(QLineEdit* edit, QWidget* parent, bool save) {
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(edit);
    auto* browse = new QPushButton(QObject::tr("浏览…"), w);
    l->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, parent, [edit, parent, save] {
        auto path = save ? QFileDialog::getSaveFileName(parent, QObject::tr("归档位置"), edit->text())
                         : QFileDialog::getExistingDirectory(parent, QObject::tr("目标目录"), edit->text());
        if (!path.isEmpty())
            edit->setText(path);
    });
    return w;
}
QComboBox* combo(QWidget* p, const QStringList& values, bool editable = false) {
    auto* c = new QComboBox(p);
    c->addItems(values);
    c->setEditable(editable);
    return c;
}
quint64 bytes(QString value) {
    QRegularExpression expression("^([0-9]+(?:\\.[0-9]+)?)\\s*([KMGT]?)(?:i?B)?$",
                                  QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(value.trimmed());
    if (!match.hasMatch())
        return 0;
    long double number = match.captured(1).toDouble();
    int exponent = match.captured(2).isEmpty() ? 0 : QString("KMGT").indexOf(match.captured(2).toUpper()) + 1;
    while (exponent-- > 0)
        number *= 1024;
    if (!std::isfinite(number) || number < 1 || number >= std::ldexp(1.0L, 64))
        return 0;
    return static_cast<quint64>(number);
}
QJsonObject createFormat(const QString& id) {
    for (const auto& f : formatCatalog()["createFormats"].toArray())
        if (f.toObject()["id"].toString() == id)
            return f.toObject();
    ArchiveEngine engine;
    for (const auto& format : engine.formats())
        if (format.canWrite && !format.isHash && !format.modulePath.empty() &&
            QString::fromStdString(format.name).compare(id, Qt::CaseInsensitive) == 0) {
            const auto extension =
                format.extensions.empty() ? id.toLower() : QString::fromStdString(format.extensions.front());
            return {
                {"id", id},
                {"label", id},
                {"extension", extension},
                {"levels", QJsonArray{}},
                {"guiMethods", QJsonArray{}},
                {"note",
                 QObject::tr("外部格式。专有参数可在“方法属性”中按 name=value 输入；可用操作由模块提供。")}};
        }
    return {};
}
} // namespace
QJsonObject formatCatalog() {
    return m7arc::loadFormatCatalog();
}
CreateDialog::CreateDialog(const QStringList& inputs, const QString& directory, QWidget* parent)
    : QDialog(parent), inputs_(inputs) {
    setWindowTitle(tr("添加到压缩包"));
    resize(820, 630);
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(14);
    auto base = inputs.size() == 1 ? QFileInfo(inputs.first()).completeBaseName() : tr("Archive");
    destination_ = new QLineEdit(QDir(directory).filePath(base + ".7z"), this);
    layout->addWidget(new QLabel(tr("归档路径与名称"), this));
    layout->addWidget(pathField(destination_, this, true));
    auto* columns = new QHBoxLayout;
    auto* left = new QGroupBox(tr("压缩参数"), this);
    auto* right = new QGroupBox(tr("更新与加密"), this);
    columns->addWidget(left, 1);
    columns->addWidget(right, 1);
    layout->addLayout(columns);
    auto* l = new QFormLayout(left);
    auto* r = new QFormLayout(right);
    l->setVerticalSpacing(10);
    r->setVerticalSpacing(10);
    l->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    r->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    format_ = new QComboBox(this);
    for (const auto& f : formatCatalog()["createFormats"].toArray()) {
        auto o = f.toObject();
        format_->addItem(o["label"].toString(), o["id"].toString());
    }
    for (const auto& preset : QStringList{"tar.gz", "tar.bz2", "tar.xz"})
        format_->addItem(preset, preset);
    ArchiveEngine engine;
    for (const auto& format : engine.formats())
        if (format.canWrite && !format.isHash && !format.modulePath.empty()) {
            const auto name = QString::fromStdString(format.name);
            if (format_->findData(name) < 0)
                format_->addItem(name, name);
        }
    l->addRow(tr("归档格式"), format_);
    level_ = new QComboBox(this);
    method_ = new QComboBox(this);
    dictionary_ = combo(this,
                        {tr("自动"), "1 MiB", "4 MiB", "16 MiB", "32 MiB", "64 MiB", "128 MiB", "256 MiB",
                         "512 MiB", "1 GiB"},
                        true);
    word_ = combo(this, {tr("自动"), "16", "32", "64", "128", "273"}, true);
    solid_ = combo(this, {tr("自动"), tr("非固实"), "16M", "128M", "1G", "4G", tr("全部固实")}, true);
    threads_ = new QSpinBox(this);
    threads_->setRange(0, qMax(1, QThread::idealThreadCount()));
    threads_->setSpecialValueText(tr("自动"));
    l->addRow(tr("压缩等级"), level_);
    l->addRow(tr("压缩方法"), method_);
    l->addRow(tr("字典 / 内存"), dictionary_);
    l->addRow(tr("词大小 / 阶数"), word_);
    l->addRow(tr("固实块"), solid_);
    l->addRow(tr("CPU 线程"), threads_);
    compressionMemory_ = new QLabel(this);
    decompressionMemory_ = new QLabel(this);
    l->addRow(tr("压缩内存估计"), compressionMemory_);
    l->addRow(tr("解压内存估计"), decompressionMemory_);
    volumes_ = new QLineEdit(this);
    volumes_->setPlaceholderText(tr("例如 100M 或 100M 1G"));
    raw_ = new QLineEdit(this);
    raw_->setPlaceholderText(tr("例如 mt=4; fb=64"));
    l->addRow(tr("分卷大小"), volumes_);
    l->addRow(tr("方法属性"), raw_);
    update_ = combo(this, {tr("添加并替换"), tr("更新并添加"), tr("只更新已有"), tr("同步")});
    r->addRow(tr("更新模式"), update_);
    storedPaths_ = combo(this, {tr("相对路径"), tr("完整路径"), tr("绝对路径")});
    r->addRow(tr("存储路径"), storedPaths_);
    shared_ = new QCheckBox(tr("压缩正在使用的文件"), this);
    latestTime_ = new QCheckBox(tr("归档时间使用最新文件时间"), this);
    r->addRow(shared_);
    r->addRow(latestTime_);
    deleteSources_ = new QCheckBox(tr("成功后删除源文件"), this);
    r->addRow(deleteSources_);
    password_ = new QLineEdit(this);
    password_->setEchoMode(QLineEdit::Password);
    confirmation_ = new QLineEdit(this);
    confirmation_->setEchoMode(QLineEdit::Password);
    r->addRow(tr("密码"), password_);
    r->addRow(tr("确认密码"), confirmation_);
    auto* showPassword = new QCheckBox(tr("显示密码"), this);
    r->addRow(showPassword);
    connect(showPassword, &QCheckBox::toggled, this, [this](bool on) {
        password_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        confirmation_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    encryption_ = new QComboBox(this);
    r->addRow(tr("加密方法"), encryption_);
    headers_ = new QCheckBox(tr("加密文件名"), this);
    r->addRow(headers_);
    links_ = new QCheckBox(tr("存储符号链接"), this);
    links_->setChecked(true);
    hardLinks_ = new QCheckBox(tr("存储硬链接"), this);
    preserveAtime_ = new QCheckBox(tr("保持源文件访问时间"), this);
    r->addRow(links_);
    r->addRow(hardLinks_);
    r->addRow(preserveAtime_);
    metadata_ = new QCheckBox(tr("保留 macOS 元数据"), this);
    metadata_->setChecked(QSettings().value("archive/macMetadata", true).toBool());
    metadata_->setToolTip(tr("保存 Finder 标签、扩展属性、资源叉、ACL 和创建时间。"));
    r->addRow(metadata_);
    auto* more = new QPushButton(tr("更多选项…"), this);
    more->setCheckable(true);
    layout->addWidget(more, 0, Qt::AlignLeft);
    auto* advanced = new QScrollArea(this);
    advanced->setWidgetResizable(true);
    auto* advancedBody = new QWidget(advanced);
    auto* advancedColumns = new QHBoxLayout(advancedBody);
    auto* advancedLeft = new QFormLayout;
    auto* advancedRight = new QFormLayout;
    advancedLeft->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    advancedRight->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    advancedColumns->addLayout(advancedLeft, 1);
    advancedColumns->addLayout(advancedRight, 1);
    advancedMethods_ = new QCheckBox(tr("显示高级压缩方法"), this);
    filter_ = new QComboBox(this);
    advancedLeft->addRow(advancedMethods_);
    advancedLeft->addRow(tr("预处理滤镜"), filter_);
    include_ = new QLineEdit(this);
    exclude_ = new QLineEdit(this);
    include_->setPlaceholderText(tr("例如 *.cpp; *.h；留空包含全部"));
    exclude_->setPlaceholderText(tr("例如 .git; node_modules"));
    advancedLeft->addRow(tr("包含匹配项"), include_);
    advancedLeft->addRow(tr("排除匹配项"), exclude_);
    ownerId_ = new QCheckBox(tr("存储所有者 ID"), this);
    ownerName_ = new QCheckBox(tr("存储所有者名称"), this);
    advancedLeft->addRow(ownerId_);
    advancedLeft->addRow(ownerName_);
    auto timeChoice = [this] { return combo(this, {tr("格式默认"), tr("存储"), tr("不存储")}); };
    mtime_ = timeChoice();
    ctime_ = timeChoice();
    atime_ = timeChoice();
    precision_ = new QComboBox(this);
    advancedRight->addRow(tr("修改时间"), mtime_);
    advancedRight->addRow(tr("创建时间"), ctime_);
    advancedRight->addRow(tr("访问时间"), atime_);
    advancedRight->addRow(tr("时间精度"), precision_);
    advanced->setWidget(advancedBody);
    advanced->setMaximumHeight(240);
    advanced->hide();
    layout->addWidget(advanced);
    connect(more, &QPushButton::toggled, this,
            [this, advanced, layout, collapsedHeight = 0](bool visible) mutable {
                const int preservedWidth = qMax(width(), 820);
                if (visible)
                    collapsedHeight = height();
                advanced->setVisible(visible);
                layout->activate();
                const int frameHeight = frameGeometry().height() - height();
                const int availableHeight = screen()->availableGeometry().height() - frameHeight - 16;
                const int extraHeight =
                    qMin(240, advanced->widget()->sizeHint().height() + advanced->frameWidth() * 2) +
                    layout->spacing();
                const int desiredHeight = visible ? collapsedHeight + extraHeight : collapsedHeight;
                resize(preservedWidth, qMin(desiredHeight, availableHeight));
            });
    connect(advancedMethods_, &QCheckBox::toggled, this, [this] {
        const auto selected = method_->currentText();
        auto id = format_->currentData().toString();
        if (id.startsWith("tar."))
            id = id == "tar.gz" ? "gzip" : id == "tar.bz2" ? "bzip2" : "xz";
        const auto spec = createFormat(id);
        method_->clear();
        for (const auto& method : spec[advancedMethods_->isChecked() ? "methods" : "guiMethods"].toArray())
            if (method.toString() != "Copy")
                method_->addItem(method.toString());
        if (id == "7z" && advancedMethods_->isChecked()) {
            ArchiveEngine engine;
            for (const auto& method : engine.codecs())
                if (method.encode && !method.filter && !method.modulePath.empty())
                    method_->addItem(QString::fromStdString(method.name));
        }
        const auto index = method_->findText(selected);
        if (index >= 0)
            method_->setCurrentIndex(index);
        updateMethod();
    });
    capability_ = new QLabel(this);
    capability_->setWordWrap(true);
    layout->addWidget(capability_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("创建 / 更新"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &CreateDialog::validate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(format_, &QComboBox::currentIndexChanged, this, &CreateDialog::updateFormat);
    connect(level_, &QComboBox::currentIndexChanged, this, &CreateDialog::updateMethod);
    connect(method_, &QComboBox::currentIndexChanged, this, &CreateDialog::updateMethod);
    connect(dictionary_, &QComboBox::currentTextChanged, this, &CreateDialog::updateMemory);
    connect(threads_, &QSpinBox::valueChanged, this, &CreateDialog::updateMemory);
    connect(raw_, &QLineEdit::textChanged, this, &CreateDialog::updateMemory);
    updateFormat();
}
void CreateDialog::setFormat(const QString& id) {
    int n = format_->findData(id);
    if (n >= 0)
        format_->setCurrentIndex(n);
}
void CreateDialog::setDestination(const QString& path) {
    destination_->setText(path);
}
QString CreateDialog::destination() const {
    return QDir::cleanPath(destination_->text());
}
void CreateDialog::updateFormat() {
    const auto id = format_->currentData().toString();
    const auto effective = id.startsWith("tar.") ? (id == "tar.gz"    ? "gzip"
                                                    : id == "tar.bz2" ? "bzip2"
                                                                      : "xz")
                                                 : id;
    auto spec = createFormat(effective);
    const auto oldSuffix = destination_->property("formatSuffix").toString();
    const auto suffix = id.startsWith("tar.") ? id : spec["extension"].toString();
    QString path = destination_->text();
    if (!oldSuffix.isEmpty() && path.endsWith('.' + oldSuffix))
        path.chop(oldSuffix.size() + 1);
    else if (oldSuffix.isEmpty() && path.endsWith(".7z"))
        path.chop(3);
    destination_->setText(path + '.' + suffix);
    destination_->setProperty("formatSuffix", suffix);
    level_->clear();
    for (const auto& v : spec["levels"].toArray())
        level_->addItem(v.toInt() == 0 ? tr("0 — 存储") : QString::number(v.toInt()), v.toInt());
    level_->setCurrentIndex(qMax(0, level_->findData(spec["defaultLevel"].toInt())));
    level_->setEnabled(level_->count() > 0);
    method_->clear();
    const bool advanced = effective == "7z" && advancedMethods_->isChecked();
    for (const auto& v : spec[advanced ? "methods" : "guiMethods"].toArray())
        if (v.toString() != "Copy" || !advanced)
            method_->addItem(v.toString());
    if (advanced) {
        ArchiveEngine engine;
        for (const auto& method : engine.codecs())
            if (method.encode && !method.filter && !method.modulePath.empty())
                method_->addItem(QString::fromStdString(method.name));
    }
    encryption_->clear();
    for (const auto& v : spec["encryption"].toArray())
        encryption_->addItem(v.toString());
    bool encrypted = encryption_->count() > 0;
    password_->setEnabled(encrypted);
    confirmation_->setEnabled(encrypted);
    encryption_->setEnabled(encrypted);
    headers_->setEnabled(spec["headerEncryption"].toBool());
    if (!headers_->isEnabled())
        headers_->setChecked(false);
    solid_->setEnabled(spec["solid"].toBool());
    threads_->setEnabled(spec["multithread"].toBool());
    dictionary_->setEnabled(effective != "tar" && effective != "wim");
    word_->setEnabled(dictionary_->isEnabled());
    const bool supportsMetadata =
        id == "7z" || id == "zip" || id == "tar" || id == "wim" || id.startsWith("tar.");
    metadata_->setEnabled(supportsMetadata);
    links_->setEnabled(supportsMetadata);
    hardLinks_->setEnabled(id == "tar" || id == "wim" || id.startsWith("tar."));
    advancedMethods_->setEnabled(effective == "7z");
    filter_->clear();
    filter_->addItem(tr("自动"), "");
    if (effective == "7z" || effective == "xz") {
        filter_->addItem(tr("关闭"), "off");
        for (const auto& filter : formatCatalog()["filters"].toObject()[effective].toArray()) {
            const auto name = filter.toString();
            if (!name.isEmpty() && name.front().unicode() < 128 && name != "Automatic" && name != "Off")
                filter_->addItem(name, name);
        }
    }
    filter_->setEnabled(effective == "7z" || effective == "xz");
    const bool supportsTimes = effective == "7z" || effective == "zip" || effective == "tar";
    for (auto* control : {mtime_, ctime_, atime_})
        control->setEnabled(supportsTimes);
    precision_->clear();
    precision_->addItem(tr("格式默认"), -1);
    if (effective == "7z" || effective == "zip")
        precision_->addItem(tr("100 纳秒"), 23);
    if (effective == "zip") {
        precision_->addItem(tr("Unix 秒"), 1);
        precision_->addItem(tr("DOS 时间"), 2);
    }
    precision_->setEnabled(precision_->count() > 1);
    {
        QSignalBlocker block(solid_);
        solid_->clear();
        solid_->addItem(tr("自动"));
        if (effective == "7z")
            solid_->addItem(tr("非固实"));
        solid_->addItems({"16M", "128M", "1G", "4G"});
        solid_->addItem(effective == "xz" ? tr("单块") : tr("全部固实"));
    }
    solid_->setEnabled(effective == "7z" || effective == "xz");
    capability_->setText(id.startsWith("tar.")
                             ? tr("按两个阶段执行：打包 TAR → 压缩流。") + spec["note"].toString()
                             : spec["note"].toString());
    updateMethod();
}
void CreateDialog::updateMethod() {
    auto id = format_->currentData().toString();
    if (id.startsWith("tar."))
        id = id == "tar.gz" ? "gzip" : id == "tar.bz2" ? "bzip2" : "xz";
    const bool compressing = level_->currentData().toUInt() != 0;
    method_->setEnabled(compressing || id == "tar");
    const auto method = method_->currentText();
    const auto details = formatCatalog()["methodDetails"].toObject()[method].toObject();
    const bool fixed = method == "Deflate" || method == "Deflate64" || id == "tar" || id == "wim";
    const auto previousDictionary = dictionary_->currentText();
    {
        QSignalBlocker block(dictionary_);
        dictionary_->clear();
        dictionary_->addItem(tr("自动"));
        for (const auto& value : details["dictionaries"].toArray())
            if (value.toString() != "自动" && value.toString() != "Automatic")
                dictionary_->addItem(value.toString());
        const auto index = dictionary_->findText(previousDictionary);
        if (index >= 0)
            dictionary_->setCurrentIndex(index);
    }
    dictionary_->setEnabled(compressing && !fixed);
    const auto previousWord = word_->currentText();
    {
        QSignalBlocker block(word_);
        word_->clear();
        word_->addItem(tr("自动"));
        for (const auto& value : details["wordSizes"].toArray())
            if (id != "zip" || method != "PPMd" || value.toInt() <= 16)
                word_->addItem(QString::number(value.toInt()));
        const auto index = word_->findText(previousWord);
        if (index >= 0)
            word_->setCurrentIndex(index);
    }
    word_->setEnabled(compressing && word_->count() > 1);
    updateMemory();
}
void CreateDialog::updateMemory() {
    if (!compressionMemory_)
        return;
    const auto estimate = ArchiveEngine::estimateMemory(options());
    compressionMemory_->setText(estimate.compressBytes ? QLocale().formattedDataSize(*estimate.compressBytes)
                                                       : tr("由高级方法配置决定"));
    decompressionMemory_->setText(estimate.decompressBytes
                                      ? QLocale().formattedDataSize(*estimate.decompressBytes)
                                      : tr("由高级方法配置决定"));
    compressionMemory_->setToolTip(QString::fromStdString(estimate.note));
}
void CreateDialog::validate() {
    if (destination().isEmpty()) {
        QMessageBox::warning(this, tr("缺少路径"), tr("请选择归档输出位置。"));
        return;
    }
    if (password_->isEnabled() && password_->text() != confirmation_->text()) {
        QMessageBox::warning(this, tr("密码不一致"), tr("两次输入的密码必须一致。"));
        return;
    }
    auto id = format_->currentData().toString();
    if (createFormat(id)["singleStream"].toBool() &&
        (inputs_.size() != 1 || QFileInfo(inputs_.first()).isDir())) {
        QMessageBox::warning(this, tr("单文件压缩流"),
                             tr("此格式只接受一个文件。多个文件或目录请选择 TAR 组合格式。"));
        return;
    }
    if (dictionary_->isEnabled() && dictionary_->currentText() != tr("自动") &&
        !bytes(dictionary_->currentText())) {
        QMessageBox::warning(this, tr("字典大小无效"), tr("输入带 K/M/G 后缀的大小，或选择自动。"));
        return;
    }
    if (solid_->isEnabled() && solid_->currentText() != tr("自动") && solid_->currentText() != tr("非固实") &&
        solid_->currentText() != tr("全部固实") && solid_->currentText() != tr("单块") &&
        !bytes(solid_->currentText())) {
        QMessageBox::warning(this, tr("块大小无效"), tr("输入带 K/M/G 后缀的大小，或选择列表中的选项。"));
        return;
    }
    for (const auto& v : volumes_->text().split(QRegularExpression("[;,\\s]+"), Qt::SkipEmptyParts))
        if (!bytes(v)) {
            QMessageBox::warning(this, tr("分卷大小无效"), tr("使用正整数和 K/M/G/T 后缀，例如 100M。"));
            return;
        }
    for (const auto& p : raw_->text().split(';', Qt::SkipEmptyParts))
        if (!p.contains('=')) {
            QMessageBox::warning(this, tr("方法属性无效"), tr("每项使用 name=value，以分号分隔。"));
            return;
        }
    accept();
}
CreateOptions CreateDialog::options() const {
    CreateOptions o;
    o.format = format_->currentData().toString().toStdString();
    o.level = level_->currentData().toUInt();
    o.method = method_->isEnabled() ? method_->currentText().toStdString() : std::string{};
    o.dictionaryBytes = dictionary_->isEnabled() ? bytes(dictionary_->currentText()) : 0;
    o.wordSize = word_->isEnabled() ? word_->currentText().toUInt() : 0;
    o.threads = threads_->isEnabled() ? threads_->value() : 0;
    o.solid = solid_->isEnabled() && solid_->currentText() != tr("非固实");
    if (solid_->isEnabled()) {
        const auto block = solid_->currentText();
        if (block == tr("全部固实"))
            o.solidBlock = "18446744073709551615b";
        else if (block == tr("单块"))
            o.solidBlock = "on";
        else if (block != tr("自动") && block != tr("非固实"))
            o.solidBlock = block.toStdString();
    }
    o.password = password_->isEnabled() ? password_->text().toStdString() : std::string{};
    o.encryptHeaders = headers_->isEnabled() && headers_->isChecked();
    o.encryptionMethod = encryption_->isEnabled() ? encryption_->currentText().toStdString() : std::string{};
    o.updateMode = static_cast<UpdateMode>(update_->currentIndex());
    o.deleteAfterCompressing = deleteSources_->isChecked();
    o.storeSymbolicLinks = links_->isEnabled() && links_->isChecked();
    o.storeHardLinks = hardLinks_->isEnabled() && hardLinks_->isChecked();
    o.preserveAccessTime = preserveAtime_->isChecked();
    o.preserveMacMetadata = metadata_->isEnabled() && metadata_->isChecked();
    o.pathMode = static_cast<ArchivePathMode>(storedPaths_->currentIndex());
    o.openSharedFiles = shared_->isChecked();
    o.setArchiveMTime = latestTime_->isChecked();
    o.storeOwnerId = ownerId_->isChecked();
    o.storeOwnerName = ownerName_->isChecked();
    for (const auto& pattern : include_->text().split(';', Qt::SkipEmptyParts))
        o.includePatterns.push_back(pattern.trimmed().toStdString());
    for (const auto& pattern : exclude_->text().split(';', Qt::SkipEmptyParts))
        o.excludePatterns.push_back(pattern.trimmed().toStdString());
    if (filter_->isEnabled() && !filter_->currentData().toString().isEmpty())
        o.properties.emplace_back("f", filter_->currentData().toString().toStdString());
    for (const auto& [name, control] :
         QList<QPair<QString, QComboBox*>>{{"tm", mtime_}, {"tc", ctime_}, {"ta", atime_}})
        if (control->isEnabled() && control->currentIndex() != 0)
            o.properties.emplace_back(name.toStdString(), control->currentIndex() == 1 ? "on" : "off");
    if (precision_->isEnabled() && precision_->currentData().toInt() >= 0)
        o.properties.emplace_back("tp", std::to_string(precision_->currentData().toInt()));

    for (const auto& v : volumes_->text().split(QRegularExpression("[;,\\s]+"), Qt::SkipEmptyParts))
        o.volumeSizes.push_back(bytes(v));
    for (auto p : raw_->text().split(';', Qt::SkipEmptyParts)) {
        int eq = p.indexOf('=');
        o.properties.emplace_back(p.left(eq).trimmed().toStdString(), p.mid(eq + 1).trimmed().toStdString());
    }
    return o;
}
ExtractDialog::ExtractDialog(const QString& archive, const QString& destination, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("提取文件"));
    resize(630, 360);
    auto* l = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    destination_ = new QLineEdit(destination, this);
    form->addRow(tr("目标目录"), pathField(destination_, this, false));
    subfolder_ = new QCheckBox(tr("创建同名子文件夹：%1").arg(QFileInfo(archive).completeBaseName()), this);
    form->addRow(subfolder_);
    pathMode_ = combo(this, {tr("保留完整路径"), tr("不含路径（平铺）")});
    pathMode_->addItem(tr("相对当前归档目录"));
    pathMode_->addItem(tr("绝对路径（高级）"));
    qobject_cast<QStandardItemModel*>(pathMode_->model())->item(2)->setEnabled(false);
    overwrite_ = combo(this, {tr("询问"), tr("直接替换"), tr("跳过已有"), tr("自动重命名传入文件"),
                              tr("自动重命名已有文件")});
    form->addRow(tr("路径模式"), pathMode_);
    form->addRow(tr("覆盖模式"), overwrite_);
    duplicateRoot_ = new QCheckBox(tr("消除重复根目录"), this);
    form->addRow(duplicateRoot_);
    metadata_ = new QCheckBox(tr("恢复 macOS 元数据"), this);
    metadata_->setChecked(QSettings().value("archive/macMetadata", true).toBool());
    form->addRow(metadata_);
    password_ = new QLineEdit(this);
    password_->setEchoMode(QLineEdit::Password);
    form->addRow(tr("密码（可按需输入）"), password_);
    auto* show = new QCheckBox(tr("显示密码"), this);
    form->addRow(show);
    connect(show, &QCheckBox::toggled, this,
            [this](bool on) { password_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
    l->addLayout(form);
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    b->button(QDialogButtonBox::Ok)->setText(tr("提取"));
    l->addWidget(b);
    connect(b, &QDialogButtonBox::accepted, this, [this] {
        if (!destination_->text().trimmed().isEmpty())
            accept();
    });
    connect(b, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
QString ExtractDialog::destination() const {
    return QDir::cleanPath(destination_->text());
}
void ExtractDialog::setCurrentDirectory(const QString& directory) {
    currentDirectory_ = directory;
    qobject_cast<QStandardItemModel*>(pathMode_->model())->item(2)->setEnabled(!directory.isEmpty());
    if (!directory.isEmpty())
        pathMode_->setCurrentIndex(2);
}
ExtractOptions ExtractDialog::options() const {
    ExtractOptions o;
    o.password = password_->text().toStdString();
    o.pathMode = static_cast<PathMode>(pathMode_->currentIndex());
    o.currentDirectory = currentDirectory_.toStdString();
    o.overwrite = static_cast<Overwrite>(overwrite_->currentIndex());
    o.createArchiveFolder = subfolder_->isChecked();
    o.eliminateDuplicateRoot = duplicateRoot_->isChecked();
    o.restoreMacMetadata = metadata_->isChecked();
    return o;
}
void showFormatCatalog(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(
        QObject::tr("格式与方法 — 7-Zip %1").arg(QString::fromStdString(ArchiveEngine::version())));
    dialog.resize(840, 580);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);
    ArchiveEngine engine;
    auto makeTable = [&](const QString& title, const QStringList& columns) {
        auto* page = new QWidget(tabs);
        auto* l = new QVBoxLayout(page);
        auto* search = new QLineEdit(page);
        search->setPlaceholderText(QObject::tr("按名称或扩展名搜索…"));
        l->addWidget(search);
        auto* table = new QTableWidget(0, columns.size(), page);
        table->setHorizontalHeaderLabels(columns);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
        l->addWidget(table);
        QObject::connect(search, &QLineEdit::textChanged, table, [table](const QString& q) {
            for (int r = 0; r < table->rowCount(); ++r) {
                bool visible = false;
                for (int c = 0; c < table->columnCount(); ++c)
                    visible |=
                        table->item(r, c) && table->item(r, c)->text().contains(q, Qt::CaseInsensitive);
                table->setRowHidden(r, !visible);
            }
        });
        tabs->addTab(page, title);
        return table;
    };
    auto add = [](QTableWidget* t, const QStringList& v) {
        int r = t->rowCount();
        t->insertRow(r);
        for (int c = 0; c < v.size(); ++c)
            t->setItem(r, c, new QTableWidgetItem(v[c]));
    };
    auto fs = engine.formats();
    auto* f =
        makeTable(QObject::tr("归档与容器 (%1)").arg(fs.size()),
                  {QObject::tr("格式"), QObject::tr("扩展名"), QObject::tr("能力"), QObject::tr("说明")});
    for (const auto& x : fs) {
        QStringList ext;
        for (const auto& e : x.extensions)
            ext << QString::fromStdString(e);
        QString note;
        for (const auto& v : formatCatalog()["formats"].toArray()) {
            auto o = v.toObject();
            if (o["name"].toString().compare(QString::fromStdString(x.name), Qt::CaseInsensitive) == 0) {
                note = o["note"].toString();
                break;
            }
        }
        add(f, {QString::fromStdString(x.name), ext.join(", "),
                x.canWrite ? (x.isHash ? QObject::tr("清单读写") : QObject::tr("读取 / 创建"))
                           : QObject::tr("只读"),
                note});
    }
    auto codecs = engine.codecs();
    auto* c = makeTable(QObject::tr("编码方法 (%1)").arg(codecs.size()),
                        {QObject::tr("名称"), QObject::tr("编码"), QObject::tr("解码")});
    for (const auto& x : codecs)
        add(c, {QString::fromStdString(x.name), x.encode ? QObject::tr("支持") : "—",
                x.decode ? QObject::tr("支持") : "—"});
    auto hashes = engine.hashers();
    auto* h = makeTable(QObject::tr("摘要算法 (%1)").arg(hashes.size()),
                        {QObject::tr("名称"), QObject::tr("摘要位数")});
    for (const auto& x : hashes)
        add(h, {QString::fromStdString(x.name), QString::number(x.digestSize * 8)});
    const auto modules = engine.modules();
    auto* modulesTable = makeTable(QObject::tr("外部模块 (%1)").arg(modules.size()),
                                   {QObject::tr("路径"), QObject::tr("状态"), QObject::tr("接口版本"),
                                    QObject::tr("格式 / 编码 / 摘要"), QObject::tr("错误详情")});
    for (const auto& module : modules)
        add(modulesTable,
            {QString::fromStdString(module.path),
             module.loaded ? QObject::tr("已加载") : QObject::tr("加载失败"), QString::number(module.version),
             QString("%1 / %2 / %3")
                 .arg(module.formats.size())
                 .arg(module.codecs.size())
                 .arg(module.hashers.size()),
             QString::fromStdString(module.error)});
    QStringList directories;
    for (const auto& directory : engine.moduleDirectories())
        directories << QString::fromStdString(directory);
    auto* modulePaths =
        new QLabel(QObject::tr("模块目录（更改模块后重新启动）：\n%1").arg(directories.join('\n')), &dialog);
    modulePaths->setWordWrap(true);
    modulePaths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(modulePaths);
    auto* b = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(b);
    QObject::connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
void showSettings(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("设置"));
    dialog.resize(780, 570);
    QSettings settings;
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);

    auto* systemPage = new QWidget(tabs);
    auto* systemLayout = new QVBoxLayout(systemPage);
    auto* formats = new QTableWidget(0, 4, systemPage);
    formats->setHorizontalHeaderLabels(
        {QObject::tr("格式"), QObject::tr("扩展名"), QObject::tr("能力"), QObject::tr("默认应用")});
    formats->setEditTriggers(QAbstractItemView::NoEditTriggers);
    formats->setSelectionBehavior(QAbstractItemView::SelectRows);
    formats->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    formats->horizontalHeader()->setStretchLastSection(true);
    ArchiveEngine engine;
    for (const auto& format : engine.formats()) {
        auto row = formats->rowCount();
        formats->insertRow(row);
        QStringList extensions;
        for (const auto& extension : format.extensions)
            extensions << QString::fromStdString(extension);
        auto* name = new QTableWidgetItem(QString::fromStdString(format.name));
        name->setData(Qt::UserRole, extensions);
        formats->setItem(row, 0, name);
        formats->setItem(row, 1, new QTableWidgetItem(extensions.join(", ")));
        formats->setItem(
            row, 2, new QTableWidgetItem(format.canWrite ? QObject::tr("读取 / 创建") : QObject::tr("只读")));
        formats->setItem(row, 3,
                         new QTableWidgetItem(extensions.isEmpty() ? QString{}
                                                                   : m7arc::platform::defaultAppForExtension(
                                                                         extensions.first())));
    }
    systemLayout->addWidget(formats);
    auto* associationButtons = new QHBoxLayout;
    auto* associate = new QPushButton(QObject::tr("将所选格式设为默认"), systemPage);
    auto* quickActions = new QPushButton(QObject::tr("安装 Finder 快速操作"), systemPage);
    associationButtons->addWidget(associate);
    associationButtons->addWidget(quickActions);
    systemLayout->addLayout(associationButtons);
    QObject::connect(associate, &QPushButton::clicked, &dialog, [&] {
        QStringList extensions;
        for (const auto& index : formats->selectionModel()->selectedRows())
            extensions.append(formats->item(index.row(), 0)->data(Qt::UserRole).toStringList());
        extensions.removeDuplicates();
        if (extensions.isEmpty())
            return;
        const auto error = m7arc::platform::setDefaultForExtensions(extensions);
        if (!error.isEmpty())
            QMessageBox::warning(&dialog, QObject::tr("文件关联"), error);
        else
            for (const auto& index : formats->selectionModel()->selectedRows()) {
                const auto suffixes = formats->item(index.row(), 0)->data(Qt::UserRole).toStringList();
                if (!suffixes.isEmpty())
                    formats->item(index.row(), 3)
                        ->setText(m7arc::platform::defaultAppForExtension(suffixes.first()));
            }
    });
    QObject::connect(quickActions, &QPushButton::clicked, &dialog, [&] {
        const auto error = m7arc::platform::installQuickActions();
        if (!error.isEmpty())
            QMessageBox::warning(&dialog, QObject::tr("安装快速操作"), error);
        else
            QMessageBox::information(&dialog, QObject::tr("快速操作已安装"),
                                     QObject::tr("可在 Finder 右键菜单的“快速操作”中使用压缩、提取和测试。"));
    });
    auto* associationNote =
        new QLabel(QObject::tr("在 Finder 的“显示简介 → 打开方式”中也可以选择默认应用。"), systemPage);
    associationNote->setWordWrap(true);
    systemLayout->addWidget(associationNote);
    tabs->addTab(systemPage, QObject::tr("系统"));

    auto* menuPage = new QWidget(tabs);
    auto* menuLayout = new QVBoxLayout(menuPage);
    menuLayout->addWidget(new QLabel(QObject::tr("显示在菜单和工具栏中的命令"), menuPage));
    QList<QPair<QString, QCheckBox*>> menuOptions;
    const QList<QPair<QString, QString>> menuNames{{"add", QObject::tr("添加")},
                                                   {"extract", QObject::tr("提取")},
                                                   {"extract-here", QObject::tr("提取到当前位置")},
                                                   {"extract-folder", QObject::tr("提取到同名文件夹")},
                                                   {"test", QObject::tr("测试")},
                                                   {"copy", QObject::tr("复制到…")},
                                                   {"move", QObject::tr("移动到…")},
                                                   {"delete", QObject::tr("删除…")},
                                                   {"info", QObject::tr("信息")}};
    for (const auto& [id, name] : menuNames) {
        auto* option = new QCheckBox(name, menuPage);
        option->setChecked(settings.value("menus/" + id, true).toBool());
        menuOptions.append({id, option});
        menuLayout->addWidget(option);
    }
    menuLayout->addStretch();
    tabs->addTab(menuPage, QObject::tr("菜单"));

    auto* foldersPage = new QWidget(tabs);
    auto* foldersForm = new QFormLayout(foldersPage);
    auto* startup = new QLineEdit(settings.value("startup/path", QDir::homePath()).toString(), foldersPage);
    auto* temporary = new QLineEdit(settings.value("folders/temp").toString(), foldersPage);
    temporary->setPlaceholderText(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    foldersForm->addRow(QObject::tr("启动目录"), pathField(startup, foldersPage, false));
    foldersForm->addRow(QObject::tr("临时文件目录"), pathField(temporary, foldersPage, false));
    auto* temporaryNote = new QLabel(
        QObject::tr(
            "留空使用系统临时目录。包内编辑与嵌套归档的临时文件保留到会话结束；未保存的修改会在退出前询问。"),
        foldersPage);
    temporaryNote->setWordWrap(true);
    foldersForm->addRow(temporaryNote);
    tabs->addTab(foldersPage, QObject::tr("文件夹"));

    auto* programsPage = new QWidget(tabs);
    auto* programsForm = new QFormLayout(programsPage);
    auto* viewer = new QLineEdit(settings.value("tools/viewer").toString(), programsPage);
    auto* editor = new QLineEdit(settings.value("tools/editor").toString(), programsPage);
    auto* diff = new QLineEdit(settings.value("tools/diff").toString(), programsPage);
    programsForm->addRow(QObject::tr("查看器可执行程序 / 应用"), viewer);
    programsForm->addRow(QObject::tr("编辑器可执行程序 / 应用"), editor);
    programsForm->addRow(QObject::tr("Diff 可执行程序 / 应用"), diff);
    auto* programsNote = new QLabel(
        QObject::tr("留空时使用系统默认应用。支持 .app 或可执行文件路径；选中的文件作为独立参数传递。"),
        programsPage);
    programsNote->setWordWrap(true);
    programsForm->addRow(programsNote);
    tabs->addTab(programsPage, QObject::tr("查看 / 编辑 / Diff"));

    auto* generalPage = new QWidget(tabs);
    auto* generalLayout = new QVBoxLayout(generalPage);
    auto addToggle = [&](const QString& key, const QString& title, bool fallback) {
        auto* option = new QCheckBox(title, generalPage);
        option->setChecked(settings.value(key, fallback).toBool());
        generalLayout->addWidget(option);
        return option;
    };
    auto* hidden = addToggle("view/hidden", QObject::tr("显示隐藏文件"), true);
    auto* alternating = addToggle("view/alternating", QObject::tr("交替行背景"), true);
    auto* autoRefresh = addToggle("view/autoRefresh", QObject::tr("自动刷新文件夹"), true);
    auto* metadata = addToggle("archive/macMetadata", QObject::tr("创建和提取时保留 macOS 元数据"), true);
    auto* restoreWindow = addToggle("window/restore", QObject::tr("记住窗口位置和大小"), true);
    generalLayout->addStretch();
    tabs->addTab(generalPage, QObject::tr("设置"));

    auto* languagePage = new QWidget(tabs);
    auto* languageForm = new QFormLayout(languagePage);
    auto* language = new QComboBox(languagePage);
    language->addItem(QObject::tr("跟随系统"), "system");
    language->addItem("简体中文", "zh_CN");
    language->addItem("English", "en");
    language->setCurrentIndex(qMax(0, language->findData(settings.value("ui/language", "system"))));
    languageForm->addRow(QObject::tr("界面语言"), language);
    auto* languageNote = new QLabel(QObject::tr("语言更改在下次启动时生效。"), languagePage);
    languageNote->setWordWrap(true);
    languageForm->addRow(languageNote);
    tabs->addTab(languagePage, QObject::tr("语言"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!temporary->text().trimmed().isEmpty() && !QFileInfo(temporary->text()).isDir()) {
            QMessageBox::warning(&dialog, QObject::tr("临时目录不可用"),
                                 QObject::tr("请选择现有的临时文件目录。"));
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings.setValue("startup/path", startup->text());
    settings.setValue("folders/temp", temporary->text().trimmed());
    settings.setValue("tools/viewer", viewer->text());
    settings.setValue("tools/editor", editor->text());
    settings.setValue("tools/diff", diff->text());
    settings.setValue("view/hidden", hidden->isChecked());
    settings.setValue("view/alternating", alternating->isChecked());
    settings.setValue("view/autoRefresh", autoRefresh->isChecked());
    settings.setValue("archive/macMetadata", metadata->isChecked());
    settings.setValue("window/restore", restoreWindow->isChecked());
    settings.setValue("ui/language", language->currentData());
    for (const auto& [id, option] : menuOptions)
        settings.setValue("menus/" + id, option->isChecked());
}

} // namespace m7arc::ui
