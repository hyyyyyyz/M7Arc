#include "HelpDialog.h"
#include "app/Settings.h"
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

namespace m7arc::ui {
namespace {
struct Topic {
    QString title;
    QString markdown;
    QString searchText;
};
class HelpDialog final : public QDialog {
  public:
    explicit HelpDialog(QWidget* parent) : QDialog(parent) {
        setObjectName("M7ArcHelp");
        setAttribute(Qt::WA_DeleteOnClose);
        resize(960, 690);
        setMinimumSize(680, 430);
        auto* layout = new QVBoxLayout(this);
        auto* searchBar = new QHBoxLayout;
        search_ = new QLineEdit(this);
        search_->setClearButtonEnabled(true);
        language_ = new QComboBox(this);
        language_->addItem("简体中文", "zh_CN");
        language_->addItem("English", "en");
        searchBar->addWidget(search_, 1);
        searchBar->addWidget(language_);
        layout->addLayout(searchBar);
        auto* split = new QSplitter(this);
        topics_ = new QListWidget(split);
        topics_->setMinimumWidth(185);
        topics_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        topics_->setWordWrap(true);
        topics_->setSpacing(3);
        browser_ = new QTextBrowser(split);
        browser_->setOpenLinks(false);
        browser_->setOpenExternalLinks(false);
        browser_->document()->setDocumentMargin(18);
        split->addWidget(topics_);
        split->addWidget(browser_);
        split->setStretchFactor(1, 1);
        split->setSizes({230, 690});
        layout->addWidget(split, 1);
        auto* bottom = new QHBoxLayout;
        status_ = new QLabel(this);
        bottom->addWidget(status_, 1);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        bottom->addWidget(buttons);
        layout->addLayout(bottom);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
        connect(search_, &QLineEdit::textChanged, this, [this] { filter(); });
        connect(topics_, &QListWidget::currentRowChanged, this, [this] { display(); });
        connect(language_, &QComboBox::currentIndexChanged, this, [this] { load(); });
        connect(browser_, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
            if (url.scheme() == "https")
                QDesktopServices::openUrl(url);
        });
        auto* find = new QShortcut(QKeySequence::Find, this);
        connect(find, &QShortcut::activated, search_, [this] {
            search_->setFocus();
            search_->selectAll();
        });
        const auto language = app::Settings().effectiveLanguage();
        language_->setCurrentIndex(language == "en" ? 1 : 0);
        load();
    }

  private:
    bool english() const {
        return language_->currentData() == "en";
    }
    QString text(const char* zh, const char* en) const {
        return QString::fromUtf8(english() ? en : zh);
    }
    void load() {
        setWindowTitle(text("M7Arc 帮助", "M7Arc Help"));
        search_->setPlaceholderText(text("搜索主题、格式或快捷键…", "Search topics, formats or shortcuts…"));
        search_->clear();
        articles_.clear();
        QFile source(":/m7arc/help/" + language_->currentData().toString() + ".md");
        if (!source.open(QIODevice::ReadOnly)) {
            browser_->setPlainText(text("无法读取帮助文件。", "Unable to read the help file."));
            return;
        }
        const auto sections =
            QString::fromUtf8(source.readAll()).split(QRegularExpression("(?m)^# "), Qt::SkipEmptyParts);
        for (const auto& section : sections) {
            const auto newline = section.indexOf('\n');
            Topic topic;
            topic.title = newline < 0 ? section.trimmed() : section.left(newline).trimmed();
            topic.markdown = "# " + section;
            QTextDocument plain;
            plain.setMarkdown(topic.markdown);
            topic.searchText = plain.toPlainText();
            articles_.append(topic);
        }
        filter();
    }
    void filter() {
        const auto previous =
            topics_->currentItem() ? topics_->currentItem()->data(Qt::UserRole).toInt() : -1;
        const auto words = search_->text().trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        topics_->clear();
        int restore = -1;
        for (qsizetype i = 0; i < articles_.size(); ++i) {
            bool match = true;
            for (const auto& word : words)
                match &= articles_[i].searchText.contains(word, Qt::CaseInsensitive);
            if (!match)
                continue;
            auto* item = new QListWidgetItem(articles_[i].title, topics_);
            item->setData(Qt::UserRole, i);
            if (i == previous)
                restore = topics_->count() - 1;
        }
        status_->setText(text("%1 个主题 · M7Arc %2", "%1 topics · M7Arc %2")
                             .arg(topics_->count())
                             .arg(QStringLiteral(M7ARC_VERSION)));
        if (topics_->count())
            topics_->setCurrentRow(restore >= 0 ? restore : 0);
        else
            browser_->setPlainText(
                text("没有匹配的主题。请尝试其他关键词。", "No matching topics. Try another search term."));
    }
    void display() {
        if (!topics_->currentItem())
            return;
        const auto index = topics_->currentItem()->data(Qt::UserRole).toInt();
        browser_->setMarkdown(articles_.at(index).markdown);
        QList<QTextEdit::ExtraSelection> matches;
        const auto words = search_->text().trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (const auto& word : words) {
            QTextCursor cursor(browser_->document());
            for (;;) {
                cursor = browser_->document()->find(word, cursor);
                if (cursor.isNull())
                    break;
                QTextEdit::ExtraSelection match;
                match.cursor = cursor;
                match.format.setBackground(palette().color(QPalette::Highlight));
                match.format.setForeground(palette().color(QPalette::HighlightedText));
                matches.append(match);
            }
        }
        browser_->setExtraSelections(matches);
    }
    QLineEdit* search_;
    QComboBox* language_;
    QListWidget* topics_;
    QTextBrowser* browser_;
    QLabel* status_;
    QList<Topic> articles_;
};
} // namespace
void showHelp(QWidget* parent) {
    auto* help = parent->findChild<QDialog*>("M7ArcHelp", Qt::FindDirectChildrenOnly);
    if (!help)
        help = new HelpDialog(parent);
    help->show();
    help->raise();
    help->activateWindow();
}
} // namespace m7arc::ui
