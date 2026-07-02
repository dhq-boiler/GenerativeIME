#include "masks.h"
#include <unordered_map>
#include <algorithm>

namespace masks
{
    // Curated sensitive-reading table. Each entry maps a reading (what
    // fires the mask expansion) to an OPTIONAL preferred mask target:
    //   maskTarget = L""       → mask the reading itself and its
    //                            katakana equivalent. Right for kana-
    //                            only vocabulary like「ちんぽ」/「おっぱい」
    //                            where the raw kana IS the word.
    //   maskTarget = L"松葉くずし" → mask ONLY this kanji form. The
    //                                reading itself doesn't get masked
    //                                because a masked hiragana version
    //                                (「〇つばくずし」) loses the visual
    //                                anchor the kanji provides. Right
    //                                for 四十八手 style vocabulary where
    //                                the kanji surface carries semantic
    //                                weight that a reading-only mask
    //                                would erase. This was the whole
    //                                「漢字を含んだ状態でマスクするのがいい」
    //                                user request.
    // Every entry here fires on every candidate window that hits this
    // reading, so a false positive is annoying (imagine「せいこう」→
    // 〇いこう being offered when the user typed for 成功). Readings on
    // this list must be almost-always adult/vulgar in context;
    // ambiguous ones (せいこう / いく / etc.) stay off.
    static const std::unordered_map<std::wstring, std::wstring>& SensitiveMap()
    {
        static const auto* m = new std::unordered_map<std::wstring, std::wstring>{
            // === 男性器 (kana-only, mask the reading itself) ===
            {L"ちんぽ", L""}, {L"ちんこ", L""}, {L"ちんちん", L""},
            {L"おちんちん", L""}, {L"ちんぽこ", L""}, {L"ちんぽう", L""},
            {L"ちんちく", L""}, {L"ちんとり", L""},
            {L"ぺにす", L""}, {L"ぽこちん", L""}, {L"まら", L""},
            {L"ふぐり", L""}, {L"きんたま", L""}, {L"たまたま", L""},
            {L"いんけい", L""}, {L"だんこん", L""},
            {L"ずるむけ", L""}, {L"ほうけい", L""}, {L"ぱっく", L""},
            // === 女性器 ===
            {L"まんこ", L""}, {L"おまんこ", L""}, {L"おめこ", L""},
            {L"おそそ", L""}, {L"ぼぼ", L""}, {L"ちゃむ", L""},
            {L"わぎな", L""}, {L"びらびら", L""}, {L"くりとりす", L""},
            {L"くりちゃん", L""}, {L"にくひだ", L""}, {L"いんもう", L""},
            {L"あそこ", L""},
            {L"ぽるちお", L""},
            {L"じーすぽっと", L""},
            {L"じーすぽ", L""},
            // === ジャンル / 属性 (kana-only, mask reading) ===
            {L"ふたなり", L""}, {L"すかとろ", L""}, {L"ぱいぱん", L""},
            {L"ろり", L""}, {L"しょた", L""}, {L"ろりこん", L""},
            {L"しょたこん", L""},
            {L"どえす", L""}, {L"どえむ", L""},
            {L"さきゅばす", L""}, {L"いんきゅばす", L""},
            {L"いんせすと", L""}, {L"はーれむ", L""},
            {L"ぎゃくはーれむ", L""}, {L"こすぷれ", L""},
            {L"ちんぐりがえし", L""},
            {L"おなほ", L""}, {L"ばいぶ", L""}, {L"でぃるど", L""},
            // === 女性向け / BL / やおい (kana-only) ===
            {L"やおい", L""}, {L"ぼーいずらぶ", L""}, {L"びーえる", L""},
            {L"じーえる", L""}, {L"がーるずらぶ", L""},
            // === 性感染症 (STI) - chat-context softening (kanji surface
            //     for kanji entries, raw form for the katakana loanwords) ===
            {L"ばいどく",       L"梅毒"},
            {L"りんびょう",     L"淋病"},
            {L"せいきへるぺす", L"性器ヘルペス"},
            {L"せんけいこんじろーま", L"尖圭コンジローマ"},
            {L"ちつかんじだしょう",   L"膣カンジダ症"},
            {L"くらみじあ", L""}, {L"かんじだ", L""},
            {L"へるぺす", L""}, {L"こんじろーま", L""}, {L"とりこもなす", L""},
            // === 解剖学的な部位 (mask the kanji form so 睾丸/亀頭/etc.
            //     stays partly readable and the mask target is what SKK
            //     converts the reading to. Bracketing everyday senses
            //     is fine because the masks land at the tail of the
            //     candidate list, not the top.) ===
            {L"きとう",   L"亀頭"},
            {L"こうがん", L"睾丸"},
            {L"いんのう", L"陰嚢"},
            {L"いんかく", L"陰核"},
            {L"えいん",   L"会陰"},
            // === 胸部 ===
            {L"おっぱい", L""}, {L"おっぱいちゃん", L""}, {L"ちくび", L""},
            {L"ぱいぱい", L""}, {L"ぱいずり", L""}, {L"にゅうりん", L""},
            {L"にゅうとう", L""}, {L"ばすと", L""}, {L"おちち", L""},
            // === 尻 / 肛門 ===
            {L"けつ", L""}, {L"けつあな", L""}, {L"おしり", L""},
            {L"あなる", L""}, {L"こうもん", L""}, {L"あなるふぁっく", L""},
            // === 性行為 ===
            {L"せっくす", L""}, {L"はめる", L""}, {L"ふぇらちお", L""},
            {L"くんに", L""}, {L"くんにり", L""}, {L"くんにりんぐす", L""},
            {L"ぶっかけ", L""}, {L"ちんぽしゃぶり", L""},
            {L"すまた", L""}, {L"ちくびあまえ", L""},
            {L"にゅうりんふぇらちお", L""}, {L"あくめ", L""},
            {L"えっち", L""}, {L"ばっく", L""}, {L"きしゅたい", L""},
            {L"ぐらいんど", L""}, {L"ろーたー", L""},
            {L"ちんぽさわり", L""}, {L"はなあくめ", L""},
            {L"ちんぽおしゃぶり", L""}, {L"みずぜめ", L""},
            {L"いらまちお", L""}, {L"すぱんきんぐ", L""},
            {L"あへがお", L""}, {L"おほごえ", L""},
            {L"がんぎまり", L""},
            // コキ 系。実在する語彙のみ (手 / 足 / 尻)。派生形の
            // おっぱいコキ / ボディコキ / 脇コキ / 太ももコキ etc. は
            // 実在しないので入れない。「パイズリ」が胸部側の相当語。
            // 手マンは fingering を指す独立語で、こちらも派生語なし。
            {L"てこき",         L"手コキ"},
            {L"しりこき",       L"尻コキ"},
            {L"あしこき",       L"足コキ"},
            {L"てまん",         L"手マン"},
            {L"かおめんきじょう", L"顔面騎乗"},
            {L"がんめんきじょうい", L"顔面騎乗位"},
            {L"たいめんきじょうい", L"対面騎乗位"},
            // 性交体位 9 種 (per user-supplied canonical list). Skip 座位 /
            // 側位 / 立位 from the mask table because those are also
            // legitimate patient-positioning terms in nursing / medical
            // contexts and masking them would clutter non-adult chat.
            {L"せいじょうい",   L"正常位"},
            {L"だいしゅきほーるど", L""},  // カタカナ主体、reading をマスク対象に
            {L"こうはいい",     L"後背位"},
            {L"たちばっく",     L"立ちバック"},
            {L"ねばっく",       L"寝バック"},
            {L"きじょうい",     L"騎乗位"},
            {L"ぎゃくきじょうい", L"逆騎乗位"},
            {L"はいめんきじょうい", L"背面騎乗位"},
            {L"たいめんそくい",   L"対面側位"},
            {L"はいめんそくい",   L"背面側位"},
            {L"たいめんざい",     L"対面座位"},
            {L"はいめんざい",     L"背面座位"},
            {L"たいめんりつい",   L"対面立位"},
            {L"はいめんりつい",   L"背面立位"},
            {L"しんちょうい",   L"伸長位"},
            {L"くっきょくい",   L"屈曲位"},
            {L"くっきゃくい",   L"屈脚位"},
            {L"こうさい",       L"交差位"},
            // Kanji-surface targets so 中出し/種付け/淫乱 etc. stay
            // visually anchored in the masked variant.
            {L"なかだし",   L"中出し"},
            {L"たねつけ",   L"種付け"},
            {L"いんらん",   L"淫乱"},
            {L"しおふき",   L"潮吹き"},
            {L"せいかんたい", L"性感帯"},
            {L"ねとられ",   L"寝取られ"},
            {L"ねとり",     L"寝取り"},
            {L"ねとらせ",   L"寝取らせ"},
            {L"ちょうきょう", L"調教"},
            {L"しばり",     L"縛り"},
            {L"じらし",     L"焦らし"},
            {L"おもらし",   L"お漏らし"},
            {L"きんしんそうかん", L"近親相姦"},
            {L"ろしゅつ",   L"露出"},
            {L"なまはめ",   L"生ハメ"},
            {L"すいみんかん", L"睡眠姦"},
            {L"じゅくじょ", L"熟女"},
            {L"しょくしゅ", L"触手"},
            {L"どうてい",   L"童貞"},
            {L"しょじょ",   L"処女"},
            {L"ぎゃくえん", L"逆援"},
            {L"ぎゃくれいぷ", L"逆レイプ"},
            {L"りょうじょく", L"凌辱"},
            {L"ぎゃくあなる", L"逆アナル"},
            {L"おもちゃぜめ", L"玩具責め"},
            // === 女性向け / BL kanji surface (visual anchor preserved) ===
            {L"ふじょし",       L"腐女子"},
            {L"ふだんし",       L"腐男子"},
            {L"ゆめじょし",     L"夢女子"},
            {L"そううけ",       L"総受け"},
            {L"そうぜめ",       L"総攻め"},
            {L"わんこぜめ",     L"ワンコ攻め"},
            {L"おれさまぜめ",   L"俺様攻め"},
            {L"できあいぜめ",   L"溺愛攻め"},
            {L"しゅうちゃくぜめ", L"執着攻め"},
            {L"どくせんぜめ",   L"独占攻め"},
            {L"かんきんもの",   L"監禁もの"},
            // === 分泌物 ===
            {L"ざーめん", L""}, {L"せいえき", L""}, {L"ちんぽじる", L""},
            {L"あいえき", L""}, {L"ちつえき", L""}, {L"おしっこ", L""},
            {L"しょんべん", L""}, {L"かんちょう", L""},
            // === 自慰 ===
            {L"せんずり", L""}, {L"おなにー", L""}, {L"しこしこ", L""},
            {L"しこる", L""}, {L"じくり", L""}, {L"ちんぽしごき", L""},
            {L"ふぇら", L""},
            // === いく / 絶頂 ===
            {L"いくいく", L""}, {L"いっちゃう", L""},
            // === 罵倒 / 卑語 ===
            {L"くそ", L""}, {L"くそったれ", L""}, {L"くそやろう", L""},
            {L"やりまん", L""}, {L"やりちん", L""}, {L"びっち", L""},

            // === 四十八手 (mask target is the KANJI surface so the visual
            //     anchor 松葉/抱き/立ち/etc. stays visible in the mask.
            //     The reading itself isn't masked - user gets a clean
            //     [kanji form + kanji-with-one-char-masked] progression.) ===
            {L"ほんて",                L"本手"},
            {L"ちゃうす",              L"茶臼"},
            {L"さかさちゃうす",        L"逆さ茶臼"},
            {L"すわりちゃうす",        L"座り茶臼"},
            {L"ほかけちゃうす",        L"帆掛け茶臼"},
            {L"まつばくずし",          L"松葉くずし"},
            {L"たちまつば",            L"立ち松葉"},
            {L"ちどり",                L"千鳥"},
            {L"てまくら",              L"手枕"},
            {L"だきじぞう",            L"抱き地蔵"},
            {L"だきあげ",              L"抱き上げ"},
            {L"だきしめ",              L"抱き締め"},
            {L"しがらみ",              L"しがらみ"},
            {L"みやま",                L"深山"},
            {L"わけいり",              L"分け入り"},
            {L"たちがなえ",            L"立ち鼎"},
            {L"すわりがなえ",          L"座り鼎"},
            {L"うぐいすのたにわたり",  L"鶯の谷渡り"},
            {L"しゅもくぞり",          L"撞木反り"},
            {L"いわしみず",            L"岩清水"},
            {L"つばめがえし",          L"燕返し"},
            {L"おしぐるま",            L"押し車"},
            {L"あじろ",                L"網代"},
            {L"でふね",                L"出船"},
            {L"いりふね",              L"入船"},
            {L"そりばし",              L"反り橋"},
            {L"はなびし",              L"花菱"},
            {L"たちはなびし",          L"立ち花菱"},
            {L"ふかなさけ",            L"深情け"},
            {L"しのび",                L"忍び"},
            {L"うらばしご",            L"裏梯子"},
            {L"くびひきれんぼ",        L"首引き恋慕"},
            {L"にしきえのよう",        L"錦絵の陽"},
            {L"つばくらお",            L"燕尾"},
            {L"いすかのはし",          L"鶍の嘴"},
            {L"うのくちばし",          L"鵜の嘴"},
            {L"にだんがえし",          L"二段返し"},
        };
        return *m;
    }

    // Hiragana → Katakana of a whole reading. Table walk instead of the
    // usual +0x60 offset so we skip 30FC (long vowel mark) and the
    // small-form range boundaries cleanly.
    static std::wstring HiraToKata(const std::wstring& hira)
    {
        std::wstring k;
        k.reserve(hira.size());
        for (wchar_t c : hira) {
            int u = (int)c;
            if (u >= 0x3041 && u <= 0x3096) k.push_back((wchar_t)(u + 0x60));
            else k.push_back(c);
        }
        return k;
    }

    // Generate one-per-position mask variants of `s` using the mask
    // character 〇 (U+3007).
    static std::vector<std::wstring> MaskEachPosition(const std::wstring& s)
    {
        constexpr wchar_t kMask = L'〇';
        std::vector<std::wstring> out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            // Don't waste a variant on masking a position that's already
            // 〇 (would be a no-op) or on ASCII space (some kanji surfaces
            // like「お っぱい」use it as a placeholder).
            if (s[i] == kMask || s[i] == L' ') continue;
            std::wstring m = s;
            m[i] = kMask;
            out.push_back(std::move(m));
        }
        return out;
    }

    std::vector<std::wstring> Variants(const std::wstring& reading)
    {
        const auto& m = SensitiveMap();
        auto it = m.find(reading);
        if (it == m.end()) return {};

        const std::wstring& target = it->second;
        if (target.empty()) {
            // Kana-only entry: mask the reading, then also offer masked
            // katakana forms since the user might prefer カタカナ in this
            // register (「チンポ」 / 「〇ンポ」…).
            auto out = MaskEachPosition(reading);
            std::wstring kata = HiraToKata(reading);
            if (kata != reading) {
                for (auto& v : MaskEachPosition(kata))
                    out.push_back(std::move(v));
            }
            return out;
        }
        // Kanji-surface entry (四十八手 etc.): mask ONLY the kanji form
        // so the kanji visual anchor stays intact. Reading + katakana
        // masks would just look like garbled hiragana here.
        return MaskEachPosition(target);
    }
}
