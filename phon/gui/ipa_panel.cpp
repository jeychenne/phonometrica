/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <Qsci/qsciscintilla.h>
#include <phon/gui/ipa_panel.hpp>

namespace phonometrica {

// ─────────────────────────────────────────────────
//  Data tables
// ─────────────────────────────────────────────────

// A consonant cell holds up to two symbols (voiceless left, voiced right).
// nullptr means empty; the special pointer value kImpossible means the articulation
// is judged impossible (rendered as a shaded cell).
static const char *const kImpossible = "\x01";

struct ConsonantCell {
	const char *voiceless;
	const char *voiced;
};

// Pulmonic consonant table: 8 manners × 11 places.
// Manner order: Plosive, Nasal, Trill, Tap/Flap, Fricative, Lateral fricative, Approximant, Lateral approximant.
// Place order:  Bilabial, Labiodental, Dental, Alveolar, Postalveolar, Retroflex, Palatal, Velar, Uvular, Pharyngeal, Glottal.

static const char *const kMannerLabels[] = {
	"Plosive", "Nasal", "Trill", "Tap/Flap",
	"Fricative", "Lat. fric.", "Approximant", "Lat. approx."
};

static const char *const kPlaceLabels[] = {
	"Bilab.", "Labiod.", "Dental", "Alveol.", "Postalv.",
	"Retrofl.", "Palatal", "Velar", "Uvular", "Pharyn.", "Glottal"
};

static constexpr int kNManners = 8;
static constexpr int kNPlaces  = 11;

// clang-format off
static const ConsonantCell kConsonants[kNManners][kNPlaces] = {
	// Plosive
	{ {"p","b"}, {kImpossible,kImpossible}, {nullptr,nullptr}, {"t","d"}, {nullptr,nullptr}, {"\xCA\x88","\xC9\x96"}, {"c","\xc9\x9f"}, {"k","\xc9\xa1"}, {"q","\xc9\xa2"}, {nullptr,kImpossible}, {"\xca\x94",kImpossible} },
	// Nasal
	{ {nullptr,"m"}, {nullptr,"\xc9\xb1"}, {nullptr,nullptr}, {nullptr,"n"}, {nullptr,nullptr}, {nullptr,"\xc9\xb3"}, {nullptr,"\xc9\xb2"}, {nullptr,"\xc5\x8b"}, {nullptr,"\xc9\xb4"}, {kImpossible,kImpossible}, {kImpossible,kImpossible} },
	// Trill
	{ {nullptr,"\xca\x99"}, {kImpossible,kImpossible}, {nullptr,nullptr}, {nullptr,"r"}, {nullptr,nullptr}, {nullptr,nullptr}, {kImpossible,kImpossible}, {kImpossible,kImpossible}, {nullptr,"\xca\x80"}, {kImpossible,kImpossible}, {kImpossible,kImpossible} },
	// Tap/Flap
	{ {nullptr,nullptr}, {nullptr,"\xe2\xb1\xb1"}, {nullptr,nullptr}, {nullptr,"\xc9\xbe"}, {nullptr,nullptr}, {nullptr,"\xc9\xbd"}, {kImpossible,kImpossible}, {kImpossible,kImpossible}, {nullptr,nullptr}, {kImpossible,kImpossible}, {kImpossible,kImpossible} },
	// Fricative
	{ {"\xc9\xb8","\xce\xb2"}, {"f","v"}, {"\xce\xb8","\xc3\xb0"}, {"s","z"}, {"\xca\x83","\xca\x92"}, {"\xca\x82","\xca\x90"}, {"\xc3\xa7","\xca\x9d"}, {"x","\xc9\xa3"}, {"\xcf\x87","\xca\x81"}, {"\xc4\xa7","\xca\x95"}, {"h","\xc9\xa6"} },
	// Lateral fricative
	{ {kImpossible,kImpossible}, {kImpossible,kImpossible}, {nullptr,nullptr}, {"\xc9\xac","\xc9\xae"}, {nullptr,nullptr}, {nullptr,nullptr}, {nullptr,nullptr}, {nullptr,nullptr}, {kImpossible,kImpossible}, {kImpossible,kImpossible}, {kImpossible,kImpossible} },
	// Approximant
	{ {nullptr,nullptr}, {nullptr,"\xca\x8b"}, {nullptr,nullptr}, {nullptr,"\xc9\xb9"}, {nullptr,nullptr}, {nullptr,"\xc9\xbb"}, {nullptr,"j"}, {nullptr,"\xc9\xb0"}, {nullptr,nullptr}, {kImpossible,kImpossible}, {kImpossible,kImpossible} },
	// Lateral approximant
	{ {kImpossible,kImpossible}, {kImpossible,kImpossible}, {nullptr,nullptr}, {nullptr,"l"}, {nullptr,nullptr}, {nullptr,"\xc9\xad"}, {nullptr,"\xca\x8e"}, {nullptr,"\xca\x9f"}, {nullptr,nullptr}, {kImpossible,kImpossible}, {kImpossible,kImpossible} },
};
// clang-format on

// Simple symbol entry: character + tooltip.
struct SymbolEntry {
	const char *symbol;
	const char *tip;
};

// clang-format off

// ── Vowels ───────────────────────────────────────
// Laid out in a grid approximating the vowel trapezoid.
// Grid: 7 rows × 6 columns.  Row/column of each entry.
struct VowelEntry {
	int row;
	int col;
	const char *symbol;
	const char *tip;
};

static const VowelEntry kVowels[] = {
	// Close
	{0, 0, "i", "close front unrounded"},
	{0, 1, "y", "close front rounded"},
	{0, 2, "\xc9\xa8", "close central unrounded"},   // ɨ
	{0, 3, "\xca\x89", "close central rounded"},      // ʉ
	{0, 4, "\xc9\xaf", "close back unrounded"},       // ɯ
	{0, 5, "u", "close back rounded"},
	// Near-close
	{1, 1, "\xc9\xaa", "near-close near-front unrounded"},  // ɪ
	{1, 2, "\xca\x8f", "near-close near-front rounded"},    // ʏ
	{1, 4, "\xca\x8a", "near-close near-back rounded"},     // ʊ
	// Close-mid
	{2, 0, "e", "close-mid front unrounded"},
	{2, 1, "\xc3\xb8", "close-mid front rounded"},    // ø
	{2, 2, "\xc9\x98", "close-mid central unrounded"},// ɘ
	{2, 3, "\xc9\xb5", "close-mid central rounded"},  // ɵ
	{2, 4, "\xc9\xa4", "close-mid back unrounded"},   // ɤ
	{2, 5, "o", "close-mid back rounded"},
	// Mid
	{3, 3, "\xc9\x99", "mid central (schwa)"},        // ə
	// Open-mid
	{4, 0, "\xc9\x9b", "open-mid front unrounded"},   // ɛ
	{4, 1, "\xc5\x93", "open-mid front rounded"},     // œ
	{4, 2, "\xc9\x9c", "open-mid central unrounded"}, // ɜ
	{4, 3, "\xc9\x9e", "open-mid central rounded"},   // ɞ
	{4, 4, "\xca\x8c", "open-mid back unrounded"},    // ʌ
	{4, 5, "\xc9\x94", "open-mid back rounded"},      // ɔ
	// Near-open
	{5, 0, "\xc3\xa6", "near-open front unrounded"},  // æ
	{5, 3, "\xc9\x90", "near-open central"},          // ɐ
	// Open
	{6, 0, "a", "open front unrounded"},
	{6, 1, "\xc9\xb6", "open front rounded"},         // ɶ
	{6, 4, "\xc9\x91", "open back unrounded"},        // ɑ
	{6, 5, "\xc9\x92", "open back rounded"},          // ɒ
};

static const char *const kVowelRowLabels[] = {
	"Close", "Near-close", "Close-mid", "Mid", "Open-mid", "Near-open", "Open"
};
static const char *const kVowelColLabels[] = {
	"Front", "", "Central", "", "Back", ""
};
static constexpr int kVowelRows = 7;
static constexpr int kVowelCols = 6;

// ── Non-pulmonic consonants ──────────────────────

static const SymbolEntry kClicks[] = {
	{"\xca\x98", "bilabial click"},
	{"\xc7\x80", "dental click"},              // ǀ
	{"\xc7\x83", "(post)alveolar click"},      // ǃ
	{"\xc7\x82", "palatoalveolar click"},      // ǂ
	{"\xc7\x81", "alveolar lateral click"},    // ǁ
};

static const SymbolEntry kImplosives[] = {
	{"\xc9\x93", "voiced bilabial implosive"},      // ɓ
	{"\xc9\x97", "voiced dental/alveolar implosive"}, // ɗ
	{"\xca\x84", "voiced palatal implosive"},        // ʄ
	{"\xc9\xa0", "voiced velar implosive"},          // ɠ
	{"\xca\x9b", "voiced uvular implosive"},         // ʛ
};

static const SymbolEntry kEjectives[] = {
	{"\xca\xbc", "ejective diacritic"},  // ʼ
};

// ── Other symbols ────────────────────────────────

static const SymbolEntry kOtherSymbols[] = {
	{"\xca\x8d", "voiceless labial-velar fricative"},   // ʍ
	{"w", "voiced labial-velar approximant"},
	{"\xc9\xa5", "voiced labial-palatal approximant"},  // ɥ
	{"\xca\x9c", "voiceless epiglottal fricative"},     // ʜ
	{"\xca\xa2", "voiced epiglottal fricative"},        // ʢ
	{"\xca\xa1", "epiglottal plosive"},                 // ʡ
	{"\xc9\x95", "alveolo-palatal voiceless fricative"},// ɕ
	{"\xca\x91", "alveolo-palatal voiced fricative"},   // ʑ
	{"\xc9\xba", "voiced alveolar lateral flap"},       // ɺ
	{"\xc9\xa7", "simultaneous \xca\x83 and x"},        // ɧ
};

// ── Diacritics ───────────────────────────────────
// Combining characters need a dotted circle (◌) base for display.

struct DiacriticEntry {
	const char *symbol;
	const char *tip;
	bool combining;  // true → display with ◌ prefix on button
};

static const DiacriticEntry kDiacritics[] = {
	// Combining marks (displayed with ◌)
	{"\xcc\xa5", "voiceless", true},                // ̥  (combining ring below)
	{"\xcc\xac", "voiced", true},                   // ̬  (combining caron below)
	{"\xcc\xb9", "more rounded", true},             // ̹
	{"\xcc\x9c", "less rounded", true},             // ̜
	{"\xcc\x9f", "advanced", true},                 // ̟
	{"\xcc\xa0", "retracted", true},                // ̠
	{"\xcc\x88", "centralized", true},              // ̈
	{"\xcc\xbd", "mid-centralized", true},          // ̽
	{"\xcc\xa9", "syllabic", true},                 // ̩
	{"\xcc\xaf", "non-syllabic", true},             // ̯
	{"\xcc\xa4", "breathy voiced", true},           // ̤
	{"\xcc\xb0", "creaky voiced", true},            // ̰
	{"\xcc\xbc", "linguolabial", true},             // ̼
	{"\xcc\xb4", "velarized or pharyngealized", true}, // ̴
	{"\xcc\x83", "nasalized", true},                // ̃
	{"\xcc\x9a", "no audible release", true},       // ̚
	{"\xcc\xaa", "dental", true},                   // ̪
	{"\xcc\xba", "apical", true},                   // ̺
	{"\xcc\xbb", "laminal", true},                  // ̻
	{"\xcc\x9d", "raised", true},                   // ̝
	{"\xcc\x9e", "lowered", true},                  // ̞
	{"\xcc\x98", "advanced tongue root", true},     // ̘
	{"\xcc\x99", "retracted tongue root", true},    // ̙
	// Modifier letters (display fine alone)
	{"\xca\xb0", "aspirated", false},               // ʰ
	{"\xcb\x9e", "rhoticity", false},               // ˞
	{"\xca\xb7", "labialized", false},              // ʷ
	{"\xca\xb2", "palatalized", false},             // ʲ
	{"\xcb\xa0", "velarized", false},               // ˠ
	{"\xcb\xa4", "pharyngealized", false},          // ˤ
	{"\xe2\x81\xbf", "nasal release", false},       // ⁿ
	{"\xcb\xa1", "lateral release", false},         // ˡ
};

// ── Suprasegmentals ──────────────────────────────

static const SymbolEntry kSupra[] = {
	{"\xcb\x88", "primary stress"},          // ˈ
	{"\xcb\x8c", "secondary stress"},        // ˌ
	{"\xcb\x90", "long"},                    // ː
	{"\xcb\x91", "half-long"},               // ˑ
	{"\xcc\x86", "extra-short"},             // ̆
	{"|", "minor (foot) group"},
	{"\xe2\x80\x96", "major (intonation) group"}, // ‖
	{".", "syllable break"},
	{"\xe2\x80\xbf", "linking"},             // ‿
};

// ── Tones ────────────────────────────────────────

// ── Tones ────────────────────────────────────────

struct ToneEntry {
	const char *symbol;
	const char *tip;
	bool combining;
};

static const ToneEntry kTones[] = {
	// Tone letters (Chao letters)
	{"\xcb\xa5", "extra-high level", false},         // ˥
	{"\xcb\xa6", "high level", false},               // ˦
	{"\xcb\xa7", "mid level", false},                // ˧
	{"\xcb\xa8", "low level", false},                // ˨
	{"\xcb\xa9", "extra-low level", false},          // ˩
	// Combining tone diacritics
	{"\xcc\x8b", "extra-high tone", true},           // combining double acute
	{"\xcc\x81", "high tone", true},                 // combining acute
	{"\xcc\x84", "mid tone", true},                  // combining macron
	{"\xcc\x80", "low tone", true},                  // combining grave
	{"\xcc\x8f", "extra-low tone", true},            // combining double grave
	{"\xcc\x8c", "rising", true},                    // combining caron
	{"\xcc\x82", "falling", true},                   // combining circumflex
	// Non-combining symbols
	{"\xe2\x86\x93", "downstep", false},             // ↓
	{"\xe2\x86\x91", "upstep", false},               // ↑
	{"\xe2\x86\x97", "global rise", false},          // ↗
	{"\xe2\x86\x98", "global fall", false},          // ↘
};

// clang-format on


// ─────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────

IpaPanel::IpaPanel(QWidget *parent) :
	QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	auto *tabs = new QTabWidget(this);
	tabs->setTabPosition(QTabWidget::South);
	tabs->addTab(createConsonantsTab(), tr("Consonants"));
	tabs->addTab(createVowelsTab(),     tr("Vowels"));
	tabs->addTab(createOtherTab(),      tr("Other"));
	tabs->addTab(createDiacriticsTab(), tr("Diacritics"));
	tabs->addTab(createSupraTab(),      tr("Supra"));

	layout->addWidget(tabs);

	// Track the last text editor that had focus so we can insert into it
	// even after clicking a button in this panel (which steals focus).
	connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget * /*now*/) {
		if (old && isEditor(old))
			m_target = old;
	});
}

// ─────────────────────────────────────────────────
//  Symbol insertion
// ─────────────────────────────────────────────────

void IpaPanel::insertSymbol(const QString &symbol)
{
	if (!m_target)
		return;

	if (auto *te = qobject_cast<QTextEdit *>(m_target))
	{
		te->setFocus();
		te->insertPlainText(symbol);
		return;
	}
	if (auto *pte = qobject_cast<QPlainTextEdit *>(m_target))
	{
		pte->setFocus();
		pte->insertPlainText(symbol);
		return;
	}
	if (auto *le = qobject_cast<QLineEdit *>(m_target))
	{
		le->setFocus();
		le->insert(symbol);
		return;
	}
	if (auto *sci = qobject_cast<QsciScintilla *>(m_target))
	{
		sci->setFocus();
		// QsciScintilla::insert() does not advance the cursor, so we
		// compute the new position manually. Scintilla positions are
		// byte offsets into the UTF-8 document.
		int line, index;
		sci->getCursorPosition(&line, &index);
		sci->insert(symbol);
		sci->setCursorPosition(line, index + symbol.toUtf8().size());
		return;
	}
}

bool IpaPanel::isEditor(QWidget *w)
{
	return qobject_cast<QTextEdit *>(w)
		|| qobject_cast<QPlainTextEdit *>(w)
		|| qobject_cast<QLineEdit *>(w)
		|| qobject_cast<QsciScintilla *>(w);
}

// ─────────────────────────────────────────────────
//  Button factory
// ─────────────────────────────────────────────────

QPushButton *IpaPanel::makeButton(const QString &symbol, const QString &tooltip)
{
	return makeButton(symbol, symbol, tooltip);
}

QPushButton *IpaPanel::makeButton(const QString &display, const QString &insert, const QString &tooltip)
{
	auto *btn = new QPushButton(display, this);
	btn->setFocusPolicy(Qt::NoFocus);
	btn->setToolTip(tooltip);
	btn->setFixedSize(32, 28);

	// A font that renders IPA well. Try Charis SIL / Doulos SIL / Noto Sans,
	// falling back to whatever the system provides.
	QFont font;
	font.setFamilies({
		QStringLiteral("Charis SIL"),
		QStringLiteral("Doulos SIL"),
		QStringLiteral("Noto Sans"),
	});
	font.setPointSize(13);
	btn->setFont(font);

	btn->setStyleSheet(QStringLiteral(
		"QPushButton { border: 1px solid palette(mid); border-radius: 2px; padding: 0; }"
		"QPushButton:hover { background: palette(highlight); color: palette(highlighted-text); }"
	));

	connect(btn, &QPushButton::clicked, this, [this, insert]() {
		insertSymbol(insert);
	});

	return btn;
}


// ─────────────────────────────────────────────────
//  Consonant table
// ─────────────────────────────────────────────────

QWidget *IpaPanel::createConsonantsTab()
{
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto *page = new QWidget(scroll);
	// Grid: row 0 = place headers, rows 1..kNManners = data rows.
	// Columns: 0 = manner label, then pairs of columns per place.
	auto *grid = new QGridLayout(page);
	grid->setSpacing(1);
	grid->setContentsMargins(4, 4, 4, 4);

	auto headerFont = QApplication::font();
	headerFont.setPointSize(headerFont.pointSize() - 1);
	headerFont.setBold(true);

	auto labelFont = QApplication::font();
	labelFont.setPointSize(labelFont.pointSize() - 1);

	// Column headers (places).
	for (int p = 0; p < kNPlaces; p++)
	{
		auto *lbl = new QLabel(tr(kPlaceLabels[p]), page);
		lbl->setFont(headerFont);
		lbl->setAlignment(Qt::AlignCenter);
		grid->addWidget(lbl, 0, 1 + 2 * p, 1, 2);
	}

	// Row headers + cells.
	for (int m = 0; m < kNManners; m++)
	{
		int gridRow = 1 + m;

		auto *rowLabel = new QLabel(tr(kMannerLabels[m]), page);
		rowLabel->setFont(labelFont);
		grid->addWidget(rowLabel, gridRow, 0);

		for (int p = 0; p < kNPlaces; p++)
		{
			const auto &cell = kConsonants[m][p];
			int col_vl = 1 + 2 * p;
			int col_vd = 2 + 2 * p;

			// Voiceless side.
			if (cell.voiceless == kImpossible)
			{
				auto *shade = new QWidget(page);
				shade->setFixedSize(32, 28);
				shade->setStyleSheet(QStringLiteral("background: palette(mid);"));
				grid->addWidget(shade, gridRow, col_vl);
			}
			else if (cell.voiceless)
			{
				grid->addWidget(makeButton(QString::fromUtf8(cell.voiceless), tr(kMannerLabels[m])), gridRow, col_vl);
			}

			// Voiced side.
			if (cell.voiced == kImpossible)
			{
				auto *shade = new QWidget(page);
				shade->setFixedSize(32, 28);
				shade->setStyleSheet(QStringLiteral("background: palette(mid);"));
				grid->addWidget(shade, gridRow, col_vd);
			}
			else if (cell.voiced)
			{
				grid->addWidget(makeButton(QString::fromUtf8(cell.voiced), tr(kMannerLabels[m])), gridRow, col_vd);
			}
		}
	}

	// Push grid content to the left instead of stretching across the scroll area.
	grid->setColumnStretch(1 + 2 * kNPlaces, 1);

	scroll->setWidget(page);
	return scroll;
}


// ─────────────────────────────────────────────────
//  Vowel table
// ─────────────────────────────────────────────────

QWidget *IpaPanel::createVowelsTab()
{
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto *page = new QWidget(scroll);
	auto *grid = new QGridLayout(page);
	grid->setSpacing(1);
	grid->setContentsMargins(4, 4, 4, 4);

	auto headerFont = QApplication::font();
	headerFont.setPointSize(headerFont.pointSize() - 1);
	headerFont.setBold(true);

	auto labelFont = QApplication::font();
	labelFont.setPointSize(labelFont.pointSize() - 1);

	// Column headers.
	const char *colHeaders[] = { "Front", "", "Central", "", "Back", "" };
	for (int c = 0; c < kVowelCols; c++)
	{
		if (colHeaders[c][0] != '\0')
		{
			auto *lbl = new QLabel(tr(colHeaders[c]), page);
			lbl->setFont(headerFont);
			lbl->setAlignment(Qt::AlignCenter);
			// Span 2 columns for the pair.
			grid->addWidget(lbl, 0, 1 + c, 1, (c < kVowelCols - 1 && colHeaders[c + 1][0] == '\0') ? 2 : 1);
		}
	}

	// Row headers.
	for (int r = 0; r < kVowelRows; r++)
	{
		auto *lbl = new QLabel(tr(kVowelRowLabels[r]), page);
		lbl->setFont(labelFont);
		grid->addWidget(lbl, 1 + r, 0);
	}

	// Place vowel buttons.
	for (const auto &v : kVowels)
	{
		auto *btn = makeButton(QString::fromUtf8(v.symbol), tr(v.tip));
		grid->addWidget(btn, 1 + v.row, 1 + v.col);
	}

	// Push grid content to the left.
	grid->setColumnStretch(1 + kVowelCols, 1);

	scroll->setWidget(page);
	return scroll;
}


// ─────────────────────────────────────────────────
//  Other symbols tab (non-pulmonic + misc)
// ─────────────────────────────────────────────────

static QLabel *sectionLabel(const QString &text, QWidget *parent)
{
	auto *lbl = new QLabel(text, parent);
	auto f = lbl->font();
	f.setBold(true);
	lbl->setFont(f);
	return lbl;
}

static void addSymbolRow(QGridLayout *grid, int &row, const QString &title, const SymbolEntry *entries, int count, IpaPanel *panel)
{
	grid->addWidget(sectionLabel(title, grid->parentWidget()), row, 0, 1, -1);
	row++;

	int col = 0;
	static constexpr int kMaxCols = 12;

	for (int i = 0; i < count; i++)
	{
		auto *btn = panel->makeButton(QString::fromUtf8(entries[i].symbol), QObject::tr(entries[i].tip));
		grid->addWidget(btn, row, col);
		if (++col >= kMaxCols)
		{
			col = 0;
			row++;
		}
	}
	row++;
}

QWidget *IpaPanel::createOtherTab()
{
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto *page = new QWidget(scroll);
	auto *grid = new QGridLayout(page);
	grid->setSpacing(2);
	grid->setContentsMargins(4, 4, 4, 4);

	int row = 0;

	addSymbolRow(grid, row, tr("Clicks"), kClicks, std::size(kClicks), this);
	addSymbolRow(grid, row, tr("Implosives"), kImplosives, std::size(kImplosives), this);
	addSymbolRow(grid, row, tr("Ejectives"), kEjectives, std::size(kEjectives), this);
	addSymbolRow(grid, row, tr("Other symbols"), kOtherSymbols, std::size(kOtherSymbols), this);

	// Push content to the top and left.
	grid->setRowStretch(row, 1);
	grid->setColumnStretch(12, 1);

	scroll->setWidget(page);
	return scroll;
}


// ─────────────────────────────────────────────────
//  Diacritics tab
// ─────────────────────────────────────────────────

QWidget *IpaPanel::createDiacriticsTab()
{
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto *page = new QWidget(scroll);
	auto *grid = new QGridLayout(page);
	grid->setSpacing(2);
	grid->setContentsMargins(4, 4, 4, 4);

	// Dotted circle used as a base for combining diacritics.
	static const QString kDottedCircle = QString::fromUtf8("\xe2\x97\x8c");  // ◌ U+25CC

	int row = 0;
	grid->addWidget(sectionLabel(tr("Diacritics"), page), row, 0, 1, -1);
	row++;

	int col = 0;
	static constexpr int kMaxCols = 12;

	for (const auto &d : kDiacritics)
	{
		auto insert = QString::fromUtf8(d.symbol);
		auto display = d.combining ? (kDottedCircle + insert) : insert;
		auto *btn = makeButton(display, insert, tr(d.tip));
		if (d.combining)
			btn->setFixedSize(38, 28); // slightly wider for ◌+diacritic
		grid->addWidget(btn, row, col);
		if (++col >= kMaxCols)
		{
			col = 0;
			row++;
		}
	}
	row++;

	// Push content to the top and left.
	grid->setRowStretch(row, 1);
	grid->setColumnStretch(kMaxCols, 1);

	scroll->setWidget(page);
	return scroll;
}


// ─────────────────────────────────────────────────
//  Suprasegmentals & Tones tab
// ─────────────────────────────────────────────────

QWidget *IpaPanel::createSupraTab()
{
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto *page = new QWidget(scroll);
	auto *grid = new QGridLayout(page);
	grid->setSpacing(2);
	grid->setContentsMargins(4, 4, 4, 4);

	static const QString kDottedCircle = QString::fromUtf8("\xe2\x97\x8c");  // ◌
	static constexpr int kMaxCols = 12;

	int row = 0;

	// ── Suprasegmentals section ──
	addSymbolRow(grid, row, tr("Suprasegmentals"), kSupra, std::size(kSupra), this);

	// ── Tones & accents section ──
	grid->addWidget(sectionLabel(tr("Tones & accents"), page), row, 0, 1, -1);
	row++;

	int col = 0;
	for (const auto &t : kTones)
	{
		auto insert = QString::fromUtf8(t.symbol);
		auto display = t.combining ? (kDottedCircle + insert) : insert;
		auto *btn = makeButton(display, insert, tr(t.tip));
		if (t.combining)
			btn->setFixedSize(38, 28);
		grid->addWidget(btn, row, col);
		if (++col >= kMaxCols)
		{
			col = 0;
			row++;
		}
	}
	row++;

	// Push content to the top and left.
	grid->setRowStretch(row, 1);
	grid->setColumnStretch(kMaxCols, 1);

	scroll->setWidget(page);
	return scroll;
}

} // namespace phonometrica
