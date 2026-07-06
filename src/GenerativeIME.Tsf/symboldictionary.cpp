#include "symboldictionary.h"
#include <unordered_map>
#include <algorithm>

namespace
{
    using Dict = std::unordered_map<std::wstring, std::vector<std::wstring>>;

    // Curated subset of the C# SymbolDictionary (~100 high-value entries).
    // Long tail (rare units, full ギリシャ大文字, alphabet marubeji) intentionally
    // skipped for now — easy to bolt on later by extending this initializer.
    const Dict& GetDict()
    {
        static const Dict d = {

            // 面積・体積・長さ
            {L"へいほうみりめーとる", {L"㎟", L"mm²"}},
            {L"へいほうせんちめーとる", {L"㎠", L"cm²"}},
            {L"へいほうめーとる", {L"㎡", L"m²"}},
            {L"へいほうきろめーとる", {L"㎢", L"km²"}},
            {L"りっぽうみりめーとる", {L"㎣", L"mm³"}},
            {L"りっぽうせんちめーとる", {L"㎤", L"cm³"}},
            {L"りっぽうめーとる", {L"㎥", L"m³"}},
            {L"りっぽうきろめーとる", {L"㎦", L"km³"}},
            {L"みりめーとる", {L"㎜", L"mm"}},
            {L"せんちめーとる", {L"㎝", L"cm"}},
            {L"でしめーとる", {L"㍷", L"dm"}},
            {L"めーとる", {L"m"}},
            {L"きろめーとる", {L"㎞", L"km"}},

            // 重さ
            {L"まいくろぐらむ", {L"㎍", L"μg"}},
            {L"みりぐらむ", {L"㎎", L"mg"}},
            {L"ぐらむ", {L"g"}},
            {L"きろぐらむ", {L"㎏", L"kg"}},
            {L"とん", {L"t"}},

            // 容積
            {L"まいくろりっとる", {L"㎕", L"μL"}},
            {L"みりりっとる", {L"㎖", L"mL"}},
            {L"でしりっとる", {L"㎗", L"dL"}},
            {L"りっとる", {L"ℓ", L"L"}},
            {L"きろりっとる", {L"㎘", L"kL"}},

            // 温度・圧力
            {L"せっし", {L"℃", L"°C"}},
            {L"せっしど", {L"℃", L"°C"}},
            {L"かし", {L"℉", L"°F"}},
            {L"かしど", {L"℉", L"°F"}},
            {L"ぱすかる", {L"㎩", L"Pa"}},
            {L"へくとぱすかる", {L"hPa"}},
            {L"きろぱすかる", {L"kPa"}},
            {L"めがぱすかる", {L"MPa"}},
            {L"ぎがぱすかる", {L"GPa"}},
            {L"てらぱすかる", {L"TPa"}},
            {L"ぺたぱすかる", {L"PPa"}},
            {L"えくさぱすかる", {L"EPa"}},
            {L"ぜたぱすかる", {L"ZPa"}},
            {L"よたぱすかる", {L"YPa"}},

            // 時間・周波数
            {L"みりびょう", {L"㎳", L"ms"}},
            {L"まいくろびょう", {L"㎲", L"μs"}},
            {L"なのびょう", {L"ns"}},
            {L"ぴこびょう", {L"ps"}},
            {L"ふぇむとびょう", {L"fs"}},
            {L"へるつ", {L"㎐", L"Hz"}},
            {L"きろへるつ", {L"㎑", L"kHz"}},
            {L"めがへるつ", {L"㎒", L"MHz"}},
            {L"ぎがへるつ", {L"㎓", L"GHz"}},
            {L"てらへるつ", {L"㎔", L"THz"}},
            {L"ぺたへるつ", {L"PHz"}},
            {L"えくさへるつ", {L"EHz"}},
            {L"ぜたへるつ", {L"ZHz"}},
            {L"よたへるつ", {L"YHz"}},

            // データ量
            {L"びっと", {L"bit"}},
            {L"ばいと", {L"B"}},
            {L"きろばいと", {L"㎅", L"KB", L"KiB"}},
            {L"めがばいと", {L"㎆", L"MB", L"MiB"}},
            {L"ぎがばいと", {L"GB", L"GiB"}},
            {L"てらばいと", {L"TB", L"TiB"}},
            {L"ぺたばいと", {L"PB", L"PiB"}},
            {L"えくさばいと", {L"EB", L"EiB"}},
            {L"ぜたばいと", {L"ZB", L"ZiB"}},
            {L"よたばいと", {L"YB", L"YiB"}},
            {L"きろびっと", {L"kbit", L"kb"}},
            {L"めがびっと", {L"Mbit", L"Mb"}},
            {L"ぎがびっと", {L"Gbit", L"Gb"}},
            {L"てらびっと", {L"Tbit", L"Tb"}},
            {L"ぺたびっと", {L"Pbit", L"Pb"}},
            {L"えくさびっと", {L"Ebit", L"Eb"}},

            // SI 接頭辞（単位なし、単独入力時）
            {L"きろ", {L"k"}},
            {L"めが", {L"M"}},
            {L"ぎが", {L"G"}},
            {L"てら", {L"T"}},
            {L"ぺた", {L"P"}},
            {L"えくさ", {L"E"}},
            {L"ぜた", {L"Z"}},
            {L"よた", {L"Y"}},
            {L"ろな", {L"R"}},
            {L"くえた", {L"Q"}},
            {L"みり", {L"m"}},
            {L"まいくろ", {L"μ", L"u"}},
            {L"なの", {L"n"}},
            {L"ぴこ", {L"p"}},
            {L"ふぇむと", {L"f"}},
            {L"あと", {L"a"}},
            {L"ぜぷと", {L"z"}},
            {L"よくと", {L"y"}},

            // 通貨
            {L"えん", {L"¥"}},
            {L"どる", {L"$"}},
            {L"ゆーろ", {L"€"}},
            {L"ぽんど", {L"£"}},

            // 矢印
            {L"やじるし", {L"→", L"←", L"↑", L"↓", L"⇒", L"⇐", L"⇑", L"⇓", L"↔", L"↕", L"↗", L"↘", L"↙", L"↖", L"⇄", L"⇅", L"⇆", L"⇋", L"⇌", L"⤴", L"⤵", L"➡", L"⬅", L"⬆", L"⬇"}},
            {L"みぎやじるし", {L"→", L"⇒", L"▶", L"➡", L"➜", L"➤", L"➔", L"🠊"}},
            {L"ひだりやじるし", {L"←", L"⇐", L"◀", L"⬅", L"🠈"}},
            {L"うえやじるし", {L"↑", L"⇑", L"▲", L"⬆", L"🠉"}},
            {L"したやじるし", {L"↓", L"⇓", L"▼", L"⬇", L"🠋"}},
            {L"みぎうえやじるし", {L"↗", L"⤴"}},
            {L"みぎしたやじるし", {L"↘", L"⤵"}},
            {L"ひだりうえやじるし", {L"↖"}},
            {L"ひだりしたやじるし", {L"↙"}},
            {L"みぎひだりやじるし", {L"↔", L"⇔", L"⇄", L"⇆"}},
            {L"うえしたやじるし", {L"↕", L"⇅"}},
            {L"りょうほうこうやじるし", {L"↔", L"↕", L"⇔"}},

            // 記号（バリエーション全部出し）
            {L"まる", {L"○", L"◯", L"●", L"◎", L"⊙", L"⊚", L"⊛", L"◉", L"⦿", L"◌", L"◍", L"🔵", L"🔴", L"⚪", L"⚫"}},
            {L"ばつ", {L"×", L"✕", L"✖", L"✗", L"✘", L"⨯", L"⨉", L"⊗", L"❌"}},
            {L"さんかく", {L"△", L"▲", L"▽", L"▼", L"◁", L"▷", L"◀", L"▶", L"⊿", L"🔺", L"🔻", L"🔼", L"🔽"}},
            {L"しかく", {L"□", L"■", L"◇", L"◆", L"▢", L"▣", L"◰", L"◱", L"◲", L"◳", L"▪", L"▫", L"🔲", L"🔳", L"🟦", L"🟥"}},
            {L"ろくかっけい", {L"⬡", L"⬢", L"⎔"}},
            {L"ごかっけい", {L"⬠", L"⬟"}},
            {L"ほし", {L"★", L"☆", L"✦", L"✧", L"✩", L"✪", L"✫", L"✬", L"✭", L"✮", L"✯", L"✰", L"⋆", L"✺", L"🌟", L"⭐"}},
            {L"はーと", {L"♥", L"♡", L"❤", L"❤️", L"💕", L"💖", L"💝", L"💗", L"💞", L"💓", L"💘", L"💟"}},
            {L"おんぷ", {L"♪", L"♫", L"♬", L"♩", L"𝄞", L"𝄢"}},
            {L"ちぇっく", {L"✓", L"✔", L"☑", L"✅", L"🗸"}},

            // 句読点・括弧
            {L"みつてん", {L"…", L"‥"}},
            {L"なみだっしゅ", {L"〜", L"～"}},
            {L"はてな", {L"？", L"?"}},
            {L"びっくり", {L"！", L"!"}},
            {L"かっこ", {L"「」", L"『』", L"（）", L"〔〕", L"【】", L"［］"}},

            // 括弧ペアそのものを読みとして受ける (「」+Space で全バリエーション表示)。
            // 打鍵された自身の形を index 0 に置き、他バリエーションを列挙。
            {L"「」", {L"「」", L"『』", L"【】", L"〔〕", L"［］", L"（）", L"｛｝", L"〈〉", L"《》"}},
            {L"『』", {L"『』", L"「」", L"【】", L"〔〕", L"［］", L"（）", L"｛｝", L"〈〉", L"《》"}},
            {L"（）", {L"（）", L"「」", L"『』", L"【】", L"〔〕", L"［］", L"｛｝", L"〈〉", L"《》"}},
            {L"【】", {L"【】", L"「」", L"『』", L"〔〕", L"［］", L"（）", L"｛｝", L"〈〉", L"《》"}},
            {L"〔〕", {L"〔〕", L"「」", L"『』", L"【】", L"［］", L"（）", L"｛｝", L"〈〉", L"《》"}},
            {L"［］", {L"［］", L"「」", L"『』", L"【】", L"〔〕", L"（）", L"｛｝", L"〈〉", L"《》"}},
            {L"｛｝", {L"｛｝", L"「」", L"『』", L"【】", L"〔〕", L"［］", L"（）", L"〈〉", L"《》"}},
            {L"〈〉", {L"〈〉", L"「」", L"『』", L"【】", L"〔〕", L"［］", L"（）", L"｛｝", L"《》"}},
            {L"《》", {L"《》", L"「」", L"『』", L"【】", L"〔〕", L"［］", L"（）", L"｛｝", L"〈〉"}},

            // 結合濁点・半濁点 (U+3099 / U+309A)
            // 全角「３０９９」+F5 と同じ結合マークを、読みからも入力可能に。
            // 直前の文字に吸着してレンダリングされる (任 + ゛ = 任゙, ふ + ゜ = ぷ)。
            {L"だくてん",   {L"゙"}},
            {L"はんだくてん", {L"゚"}},

            // 数学: 四則・基本演算
            {L"ぷらす", {L"+", L"＋"}},
            {L"まいなす", {L"-", L"−", L"－"}},
            {L"かける", {L"×", L"✕", L"・", L"*"}},
            {L"わる", {L"÷", L"／"}},
            {L"ぷらすまいなす", {L"±"}},
            {L"まいなすぷらす", {L"∓"}},
            {L"いこーる", {L"=", L"＝"}},

            // 数学: 比較・関係
            {L"ちいさい", {L"<", L"＜"}},
            {L"おおきい", {L">", L"＞"}},
            {L"いか", {L"≦", L"≤"}},
            {L"いじょう", {L"≧", L"≥"}},
            {L"みまん", {L"<", L"＜"}},
            {L"こえる", {L">", L"＞"}},
            {L"にあらず", {L"≠"}},
            {L"やく", {L"≒", L"約", L"≈"}},
            {L"ごうどう", {L"≡"}},
            {L"ひれい", {L"∝"}},
            {L"そうじ", {L"∽"}},

            // 数学: 集合
            {L"ぞくす", {L"∈"}},
            {L"ぞくさない", {L"∉"}},
            {L"ふくむ", {L"∋"}},
            {L"ぶぶんしゅうごう", {L"⊂", L"⊆"}},
            {L"わしゅうごう", {L"∪"}},
            {L"せきしゅうごう", {L"∩"}},
            {L"くうしゅうごう", {L"∅", L"Ø", L"φ"}},
            {L"しぜんすう", {L"ℕ", L"N"}},
            {L"せいすう", {L"ℤ", L"Z"}},
            {L"ゆうりすう", {L"ℚ", L"Q"}},
            {L"じっすう", {L"ℝ", L"R"}},
            {L"ふくそすう", {L"ℂ", L"C"}},

            // 数学: 論理
            {L"かつ", {L"∧", L"∩", L"&"}},
            {L"または", {L"∨", L"∪", L"|"}},
            {L"ひてい", {L"¬", L"！", L"~"}},
            {L"ならば", {L"⇒", L"→", L"⟹"}},
            {L"どうち", {L"⇔", L"↔", L"⟺"}},
            {L"どうちの", {L"⇔", L"↔"}},
            {L"にんいの", {L"∀"}},
            {L"そんざい", {L"∃"}},
            {L"そんざいしない", {L"∄"}},
            {L"どうよう", {L"∵"}},
            {L"ゆえに", {L"∴"}},
            {L"なぜなら", {L"∵"}},

            // 数学: 微積分（積分は全バリエーション網羅）
            {L"せきぶん", {L"∫", L"∬", L"∭", L"∮", L"∯", L"∰", L"⨌"}},
            {L"にじゅうせきぶん", {L"∬"}},
            {L"さんじゅうせきぶん", {L"∭"}},
            {L"よんじゅうせきぶん", {L"⨌"}},
            {L"しゅうせきぶん", {L"∮"}},
            {L"せんせきぶん", {L"∮"}},
            {L"めんせきぶん", {L"∯", L"∮"}},
            {L"たいせきぶん", {L"∰", L"∭"}},
            {L"みぎまわりせきぶん", {L"∱", L"∲"}},
            {L"ひだりまわりせきぶん", {L"∳"}},
            {L"へんびぶん", {L"∂"}},
            {L"なぶら", {L"∇"}},
            {L"ぞうぶん", {L"Δ", L"△", L"∆"}},
            {L"わ", {L"Σ", L"∑"}},
            {L"せき", {L"Π", L"∏"}},
            {L"きょくげん", {L"lim"}},

            // 数学: 累乗根・指数
            {L"るーと", {L"√"}},
            {L"へいほうこん", {L"√"}},
            {L"さんじょうこん", {L"∛"}},
            {L"よじょうこん", {L"∜"}},

            // 数学: 幾何
            {L"かく", {L"∠", L"角"}},
            {L"ちょっかく", {L"∟", L"⊥"}},
            {L"すいちょく", {L"⊥"}},
            {L"へいこう", {L"∥", L"//"}},
            {L"へいこうしない", {L"∦"}},
            {L"さんかくけい", {L"△", L"▲"}},
            {L"むげんだい", {L"∞"}},
            {L"ど", {L"°"}},
            {L"ぷらいむ", {L"′"}},
            {L"だぶるぷらいむ", {L"″"}},

            // 数学: 分数（よく使う分母のみ）
            {L"にぶんのいち", {L"½", L"1/2"}},
            {L"さんぶんのいち", {L"⅓", L"1/3"}},
            {L"さんぶんのに", {L"⅔", L"2/3"}},
            {L"よんぶんのいち", {L"¼", L"1/4"}},
            {L"よんぶんのさん", {L"¾", L"3/4"}},
            {L"ごぶんのいち", {L"⅕", L"1/5"}},
            {L"はちぶんのいち", {L"⅛", L"1/8"}},

            // 数学: 累乗（上付き数字フルセット 0-9）
            {L"じじょう", {L"²"}},
            {L"さんじょう", {L"³"}},
            {L"うえつき0", {L"⁰"}},
            {L"うえつき1", {L"¹"}},
            {L"うえつき2", {L"²"}},
            {L"うえつき3", {L"³"}},
            {L"うえつき4", {L"⁴"}},
            {L"うえつき5", {L"⁵"}},
            {L"うえつき6", {L"⁶"}},
            {L"うえつき7", {L"⁷"}},
            {L"うえつき8", {L"⁸"}},
            {L"うえつき9", {L"⁹"}},
            {L"うえつきぷらす", {L"⁺"}},
            {L"うえつきまいなす", {L"⁻"}},
            {L"うえつきえぬ", {L"ⁿ"}},
            {L"うえつき", {L"ⁿ", L"²", L"³", L"⁰", L"¹", L"⁴", L"⁵", L"⁶", L"⁷", L"⁸", L"⁹"}},
            {L"すーぱーすくりぷと", {L"ⁿ", L"²", L"³"}},

            // 数学: 下付き数字フルセット 0-9
            {L"したつき0", {L"₀"}},
            {L"したつき1", {L"₁"}},
            {L"したつき2", {L"₂"}},
            {L"したつき3", {L"₃"}},
            {L"したつき4", {L"₄"}},
            {L"したつき5", {L"₅"}},
            {L"したつき6", {L"₆"}},
            {L"したつき7", {L"₇"}},
            {L"したつき8", {L"₈"}},
            {L"したつき9", {L"₉"}},
            {L"したつきえぬ", {L"ₙ"}},
            {L"したつき", {L"ₙ", L"₀", L"₁", L"₂", L"₃", L"₄", L"₅", L"₆", L"₇", L"₈", L"₉"}},
            {L"さぶすくりぷと", {L"ₙ", L"₀", L"₁", L"₂"}},

            // 数字: 丸囲み
            {L"まるすうじ", {L"①", L"②", L"③", L"④", L"⑤", L"⑥", L"⑦", L"⑧", L"⑨", L"⑩"}},
            {L"まる1", {L"①", L"❶", L"⓵"}},
            {L"まる2", {L"②", L"❷", L"⓶"}},
            {L"まる3", {L"③", L"❸", L"⓷"}},
            {L"まる4", {L"④", L"❹", L"⓸"}},
            {L"まる5", {L"⑤", L"❺", L"⓹"}},
            {L"まる6", {L"⑥", L"❻", L"⓺"}},
            {L"まる7", {L"⑦", L"❼", L"⓻"}},
            {L"まる8", {L"⑧", L"❽", L"⓼"}},
            {L"まる9", {L"⑨", L"❾", L"⓽"}},
            {L"まる10", {L"⑩", L"❿", L"⓾"}},
            {L"まる0", {L"⓪", L"🅞"}},
            {L"まるえー", {L"Ⓐ", L"ⓐ"}},
            {L"まるびー", {L"Ⓑ", L"ⓑ"}},

            // ローマ数字
            {L"ろーますうじ", {L"Ⅰ", L"Ⅱ", L"Ⅲ", L"Ⅳ", L"Ⅴ", L"Ⅵ", L"Ⅶ", L"Ⅷ", L"Ⅸ", L"Ⅹ"}},
            {L"ろーま1", {L"Ⅰ", L"ⅰ"}},
            {L"ろーま2", {L"Ⅱ", L"ⅱ"}},
            {L"ろーま3", {L"Ⅲ", L"ⅲ"}},
            {L"ろーま4", {L"Ⅳ", L"ⅳ"}},
            {L"ろーま5", {L"Ⅴ", L"ⅴ"}},
            {L"ろーま6", {L"Ⅵ", L"ⅵ"}},
            {L"ろーま7", {L"Ⅶ", L"ⅶ"}},
            {L"ろーま8", {L"Ⅷ", L"ⅷ"}},
            {L"ろーま9", {L"Ⅸ", L"ⅸ"}},
            {L"ろーま10", {L"Ⅹ", L"ⅹ"}},

            // 数字: 半角ASCII入力（"5" → Ⅴ/⑤/五 等）
            // 上付き (⁰¹²³⁴⁵⁶⁷⁸⁹) と下付き (₀₁₂₃₄₅₆₇₈₉) は分母・分子や
            // 化学式・数式の入力用途で末尾に追加。SKK 直接エントリ ("2 /...")
            // は数字候補が多いので視認性のため既存の順序を崩さず末尾に。
            {L"0", {L"０", L"Ⅹ", L"⓪", L"〇", L"零", L"⁰", L"₀"}},
            {L"1", {L"１", L"Ⅰ", L"ⅰ", L"①", L"❶", L"一", L"壱", L"¹", L"₁"}},
            {L"2", {L"２", L"Ⅱ", L"ⅱ", L"②", L"❷", L"二", L"弐", L"²", L"₂"}},
            {L"3", {L"３", L"Ⅲ", L"ⅲ", L"③", L"❸", L"三", L"参", L"³", L"₃"}},
            {L"4", {L"４", L"Ⅳ", L"ⅳ", L"④", L"❹", L"四", L"肆", L"⁴", L"₄"}},
            {L"5", {L"５", L"Ⅴ", L"ⅴ", L"⑤", L"❺", L"五", L"伍", L"⁵", L"₅"}},
            {L"6", {L"６", L"Ⅵ", L"ⅵ", L"⑥", L"❻", L"六", L"陸", L"⁶", L"₆"}},
            {L"7", {L"７", L"Ⅶ", L"ⅶ", L"⑦", L"❼", L"七", L"漆", L"⁷", L"₇"}},
            {L"8", {L"８", L"Ⅷ", L"ⅷ", L"⑧", L"❽", L"八", L"捌", L"⁸", L"₈"}},
            {L"9", {L"９", L"Ⅸ", L"ⅸ", L"⑨", L"❾", L"九", L"玖", L"⁹", L"₉"}},
            {L"10", {L"１０", L"Ⅹ", L"ⅹ", L"⑩", L"❿", L"十", L"拾"}},

            // 数字: 漢数字ひらがな読み（"ご" → 5/Ⅴ/⑤/五 等）
            {L"いち", {L"1", L"１", L"Ⅰ", L"ⅰ", L"①", L"一", L"壱"}},
            {L"に", {L"2", L"２", L"Ⅱ", L"ⅱ", L"②", L"二", L"弐"}},
            {L"さん", {L"3", L"３", L"Ⅲ", L"ⅲ", L"③", L"三", L"参"}},
            {L"し", {L"4", L"４", L"Ⅳ", L"ⅳ", L"④", L"四", L"肆"}},
            {L"よん", {L"4", L"４", L"Ⅳ", L"ⅳ", L"④", L"四"}},
            {L"ご", {L"5", L"５", L"Ⅴ", L"ⅴ", L"⑤", L"五", L"伍"}},
            {L"ろく", {L"6", L"６", L"Ⅵ", L"ⅵ", L"⑥", L"六", L"陸"}},
            {L"しち", {L"7", L"７", L"Ⅶ", L"ⅶ", L"⑦", L"七", L"漆"}},
            {L"なな", {L"7", L"７", L"Ⅶ", L"ⅶ", L"⑦", L"七"}},
            {L"はち", {L"8", L"８", L"Ⅷ", L"ⅷ", L"⑧", L"八", L"捌"}},
            {L"く", {L"9", L"９", L"Ⅸ", L"ⅸ", L"⑨", L"九", L"玖"}},
            {L"きゅう", {L"9", L"９", L"Ⅸ", L"ⅸ", L"⑨", L"九"}},
            {L"じゅう", {L"10", L"１０", L"Ⅹ", L"ⅹ", L"⑩", L"十", L"拾"}},

            // 数学: その他
            {L"へいきん", {L"x̄", L"x̅", L"平均"}},
            {L"ばー", {L"‾"}},
            {L"どっと", {L"·", L"⋅"}},
            {L"くろす", {L"×"}},
            {L"てんすうのてん", {L"・", L"·"}},

            // 分数 (フラクション): "1・2" を入力して Space で ½ 等を候補に。
            // 入力バッファは digit + '/' + digit の生 ASCII だが、Hiragana
            // モードでは romaji 変換テーブルの '/' → '・' が効くため、
            // TryOllamaConvertAsync が組み立てる reading は「1・2」の形。
            // symbol 辞書のキーもそちらに合わせる。Unicode の Vulgar
            // Fraction を第一候補、代替形（1⁄2 = numerator + fraction-slash
            // + denominator）を第二候補として順不同のバリエーションで用意。
            {L"1・2", {L"½", L"1⁄2"}},
            {L"1・3", {L"⅓", L"1⁄3"}},
            {L"2・3", {L"⅔", L"2⁄3"}},
            {L"1・4", {L"¼", L"1⁄4"}},
            {L"3・4", {L"¾", L"3⁄4"}},
            {L"1・5", {L"⅕", L"1⁄5"}},
            {L"2・5", {L"⅖", L"2⁄5"}},
            {L"3・5", {L"⅗", L"3⁄5"}},
            {L"4・5", {L"⅘", L"4⁄5"}},
            {L"1・6", {L"⅙", L"1⁄6"}},
            {L"5・6", {L"⅚", L"5⁄6"}},
            {L"1・7", {L"⅐", L"1⁄7"}},
            {L"1・8", {L"⅛", L"1⁄8"}},
            {L"3・8", {L"⅜", L"3⁄8"}},
            {L"5・8", {L"⅝", L"5⁄8"}},
            {L"7・8", {L"⅞", L"7⁄8"}},
            {L"1・9", {L"⅑", L"1⁄9"}},
            {L"1・10", {L"⅒", L"1⁄10"}},
            {L"0・3", {L"↉", L"0⁄3"}},

            // 国旗絵文字 (REGIONAL INDICATOR SYMBOL LETTER pair). 単発 ASCII
            // "jp" 等は FlagFromIso2 でも拾えるが、日本語読み ("にほん" /
            // "アメリカ" 相当) からは辞書経由で候補に混ぜる必要がある。
            // よく使う国だけ列挙 — 網羅性より "とっさに出したい" のペイン解消。
            // 各キーは主要な読み1つに集約し、代替読みも別エントリで登録。
            {L"にほん",       {L"🇯🇵"}},
            {L"にっぽん",     {L"🇯🇵"}},
            {L"にほんこっき", {L"🇯🇵"}},
            {L"あめりか",     {L"🇺🇸"}},
            {L"べいこく",     {L"🇺🇸"}},
            {L"いぎりす",     {L"🇬🇧"}},
            {L"えいこく",     {L"🇬🇧"}},
            {L"かんこく",     {L"🇰🇷"}},
            {L"きたちょうせん", {L"🇰🇵"}},
            {L"ちゅうごく",   {L"🇨🇳"}},
            {L"たいわん",     {L"🇹🇼"}},
            {L"ほんこん",     {L"🇭🇰"}},
            {L"どいつ",       {L"🇩🇪"}},
            {L"ふらんす",     {L"🇫🇷"}},
            {L"いたりあ",     {L"🇮🇹"}},
            {L"すぺいん",     {L"🇪🇸"}},
            {L"ぽるとがる",   {L"🇵🇹"}},
            {L"おらんだ",     {L"🇳🇱"}},
            {L"べるぎー",     {L"🇧🇪"}},
            {L"すいす",       {L"🇨🇭"}},
            {L"おーすとりあ", {L"🇦🇹"}},
            {L"ぽーらんど",   {L"🇵🇱"}},
            {L"ちぇこ",       {L"🇨🇿"}},
            {L"はんがりー",   {L"🇭🇺"}},
            {L"ぎりしゃ",     {L"🇬🇷"}},
            {L"とるこ",       {L"🇹🇷"}},
            {L"ろしあ",       {L"🇷🇺"}},
            {L"うくらいな",   {L"🇺🇦"}},
            {L"すうぇーでん", {L"🇸🇪"}},
            {L"のるうぇー",   {L"🇳🇴"}},
            {L"でんまーく",   {L"🇩🇰"}},
            {L"ふぃんらんど", {L"🇫🇮"}},
            {L"あいすらんど", {L"🇮🇸"}},
            {L"あいるらんど", {L"🇮🇪"}},
            {L"かなだ",       {L"🇨🇦"}},
            {L"めきしこ",     {L"🇲🇽"}},
            {L"ぶらじる",     {L"🇧🇷"}},
            {L"あるぜんちん", {L"🇦🇷"}},
            {L"ちり",         {L"🇨🇱"}},
            {L"ぺるー",       {L"🇵🇪"}},
            {L"おーすとらりあ", {L"🇦🇺"}},
            {L"にゅーじーらんど", {L"🇳🇿"}},
            {L"いんど",       {L"🇮🇳"}},
            {L"ぱきすたん",   {L"🇵🇰"}},
            {L"ばんぐらでしゅ", {L"🇧🇩"}},
            {L"すりらんか",   {L"🇱🇰"}},
            {L"たい",         {L"🇹🇭"}},
            {L"べとなむ",     {L"🇻🇳"}},
            {L"いんどねしあ", {L"🇮🇩"}},
            {L"まれーしあ",   {L"🇲🇾"}},
            {L"しんがぽーる", {L"🇸🇬"}},
            {L"ふぃりぴん",   {L"🇵🇭"}},
            {L"みゃんまー",   {L"🇲🇲"}},
            {L"かんぼじあ",   {L"🇰🇭"}},
            {L"らおす",       {L"🇱🇦"}},
            {L"もんごる",     {L"🇲🇳"}},
            {L"いすらえる",   {L"🇮🇱"}},
            {L"さうじあらびあ", {L"🇸🇦"}},
            {L"いらん",       {L"🇮🇷"}},
            {L"いらく",       {L"🇮🇶"}},
            {L"えじぷと",     {L"🇪🇬"}},
            {L"みなみあふりか", {L"🇿🇦"}},
            {L"けにあ",       {L"🇰🇪"}},
            {L"えちおぴあ",   {L"🇪🇹"}},
            {L"ないじぇりあ", {L"🇳🇬"}},
            {L"もろっこ",     {L"🇲🇦"}},
            {L"えう",         {L"🇪🇺"}},
            {L"こくれん",     {L"🇺🇳"}},

            // ギリシャ文字（小文字・大文字、フルセット）
            {L"あるふぁ", {L"α", L"Α"}},
            {L"べーた", {L"β", L"Β"}},
            {L"がんま", {L"γ", L"Γ"}},
            {L"でるた", {L"δ", L"Δ"}},
            {L"えぷしろん", {L"ε", L"Ε", L"ϵ"}},
            {L"いぷしろん", {L"ε", L"Ε"}},
            {L"ぜーた", {L"ζ", L"Ζ"}},
            {L"いーた", {L"η", L"Η"}},
            {L"しーた", {L"θ", L"Θ", L"ϑ"}},
            {L"せーた", {L"θ", L"Θ"}},
            {L"いおた", {L"ι", L"Ι"}},
            {L"かっぱ", {L"κ", L"Κ"}},
            {L"らむだ", {L"λ", L"Λ"}},
            {L"みゅー", {L"μ", L"Μ"}},
            {L"にゅー", {L"ν", L"Ν"}},
            {L"くしー", {L"ξ", L"Ξ"}},
            {L"ぐざい", {L"ξ", L"Ξ"}},
            {L"くさい", {L"ξ", L"Ξ"}},
            {L"おみくろん", {L"ο", L"Ο"}},
            {L"ぱい", {L"π", L"Π", L"ϖ"}},
            {L"ろー", {L"ρ", L"Ρ", L"ϱ"}},
            {L"しぐま", {L"σ", L"Σ", L"ς"}},
            {L"たう", {L"τ", L"Τ"}},
            {L"うぷしろん", {L"υ", L"Υ"}},
            {L"ふぁい", {L"φ", L"Φ", L"ϕ"}},
            {L"かい", {L"χ", L"Χ"}},
            {L"ぷさい", {L"ψ", L"Ψ"}},
            {L"ぷしー", {L"ψ", L"Ψ"}},
            {L"おめが", {L"ω", L"Ω"}},

            // 英語呼び（数学・論理）
            {L"いんてぐらる", {L"∫", L"∬", L"∭", L"∮", L"∯", L"∰", L"⨌"}},
            {L"いんてぐ", {L"∫", L"∬", L"∭", L"∮"}},
            {L"だぶるいんてぐらる", {L"∬"}},
            {L"とりぷるいんてぐらる", {L"∭"}},
            {L"こんつあーいんてぐらる", {L"∮"}},
            {L"さーふぇすいんてぐらる", {L"∯"}},
            {L"ぼりゅーむいんてぐらる", {L"∰"}},
            {L"さめーしょん", {L"Σ", L"∑"}},
            {L"さむ", {L"Σ", L"∑"}},
            {L"ぷろだくと", {L"Π", L"∏"}},
            {L"ぷろど", {L"Π", L"∏"}},
            {L"ぱーしゃる", {L"∂"}},
            {L"でる", {L"∂", L"∇"}},
            {L"いんふぃにてぃ", {L"∞"}},
            {L"いんふぃに", {L"∞"}},
            {L"すくえあるーと", {L"√"}},
            {L"きゅーびっくるーと", {L"∛"}},
            {L"どっとぷろだくと", {L"·", L"⋅"}},
            {L"くろすぷろだくと", {L"×"}},

            // 英語呼び（比較・関係）
            {L"えくおる", {L"=", L"＝"}},
            {L"のっといこーる", {L"≠"}},
            {L"のといこ", {L"≠"}},
            {L"あぷろくす", {L"≈", L"≒"}},
            {L"あぷろっくす", {L"≈", L"≒"}},
            {L"れす", {L"<", L"＜"}},
            {L"れすざん", {L"<", L"＜"}},
            {L"ぐれーた", {L">", L"＞"}},
            {L"ぐれーたーざん", {L">", L"＞"}},
            {L"れすいこーる", {L"≤", L"≦"}},
            {L"ぐれーたーいこーる", {L"≥", L"≧"}},
            {L"ぷろぽーしょなる", {L"∝"}},
            {L"しみらー", {L"∽", L"～"}},
            {L"こんぐるーえんと", {L"≅", L"≡"}},

            // 英語呼び（集合）
            {L"いん", {L"∈"}},
            {L"めんばーおぶ", {L"∈"}},
            {L"なっといん", {L"∉"}},
            {L"さぶせっと", {L"⊂", L"⊆"}},
            {L"すーぱーせっと", {L"⊃", L"⊇"}},
            {L"ゆにおん", {L"∪"}},
            {L"いんたーせくしょん", {L"∩"}},
            {L"いんた", {L"∩"}},
            {L"えんぷてぃー", {L"∅", L"Ø"}},
            {L"えんぷてぃーせっと", {L"∅", L"Ø"}},
            {L"なちゅらる", {L"ℕ"}},
            {L"いんてじゃー", {L"ℤ"}},
            {L"れあーる", {L"ℝ"}},
            {L"こんぷれっくす", {L"ℂ"}},

            // 英語呼び（論理）
            {L"あんど", {L"∧", L"&", L"＆"}},
            {L"おあ", {L"∨", L"|", L"｜"}},
            {L"のっと", {L"¬", L"!", L"～"}},
            {L"いんぷらい", {L"⇒", L"→"}},
            {L"いんぷらいず", {L"⇒", L"→"}},
            {L"いふあんどおんりーいふ", {L"⇔", L"↔"}},
            {L"いふいふ", {L"⇔", L"↔"}},
            {L"ふぉーおーる", {L"∀"}},
            {L"ふぉーら", {L"∀"}},
            {L"えくぞすつ", {L"∃"}},
            {L"えぐじすつ", {L"∃"}},
            {L"ぜあふぉあ", {L"∴"}},
            {L"びこーず", {L"∵"}},

            // 英語呼び（基本演算）
            {L"あど", {L"+", L"＋"}},
            {L"さぶ", {L"-", L"−", L"－"}},
            {L"たいむず", {L"×", L"✕", L"*"}},
            {L"まるち", {L"×", L"✕", L"*"}},
            {L"でぃばいど", {L"÷", L"／"}},
            {L"ぷらすまい", {L"±"}},
            {L"でぃぐりー", {L"°"}},
            {L"あんぐる", {L"∠"}},

            // 英語呼び（幾何）
            {L"ぱーぺんでぃきゅらー", {L"⊥"}},
            {L"ぱられる", {L"∥", L"//"}},
            {L"とらいあんぐる", {L"△", L"▲"}},
            {L"すくえあ", {L"□", L"■"}},
            {L"さーくる", {L"○", L"◯", L"●"}},

            // 英語呼び（記号）
            {L"すたー", {L"★", L"☆", L"✦"}},
            {L"あろー", {L"→", L"←", L"↑", L"↓"}},
            {L"みぎあろー", {L"→", L"⇒"}},
            {L"ひだりあろー", {L"←", L"⇐"}},
            {L"うえあろー", {L"↑", L"⇑"}},
            {L"したあろー", {L"↓", L"⇓"}},
            {L"みゅーじっく", {L"♪", L"♫", L"♬", L"♩"}},
            {L"のーと", {L"♪", L"♫"}},
            {L"はっしゅ", {L"#", L"＃"}},
            {L"あっとまーく", {L"@", L"＠"}},

            // その他
            {L"きごう", {L"♂", L"♀", L"§", L"¶", L"©", L"®", L"™"}},

            // 性別
            {L"おす", {L"♂"}},
            {L"めす", {L"♀"}},
            {L"だんせい", {L"♂"}},
            {L"じょせい", {L"♀"}},
            {L"おとこ", {L"♂", L"男"}},
            {L"おんな", {L"♀", L"女"}},
            {L"まーず", {L"♂"}},
            {L"びーなす", {L"♀"}},

            // 知的財産・出版
            {L"ちょさくけん", {L"©", L"著作権"}},
            {L"ちょさっけん", {L"©", L"著作権"}},
            {L"こぴーらいと", {L"©"}},
            {L"こぴらいと", {L"©"}},
            {L"とうろくしょうひょう", {L"®", L"登録商標"}},
            {L"れじすとあーど", {L"®"}},
            {L"れじすたー", {L"®"}},
            {L"しょうひょう", {L"™", L"商標"}},
            {L"とれーどまーく", {L"™"}},
            {L"せくしょん", {L"§"}},
            {L"せつ", {L"§", L"節"}},
            {L"ぱらぐらふ", {L"¶", L"§"}},
            {L"だがー", {L"†"}},
            {L"だぶるだがー", {L"‡"}},

            // その他記号
            {L"あっと", {L"@", L"＠"}},
            {L"しゃーぷ", {L"#", L"＃", L"♯"}},
            {L"ふらっと", {L"♭"}},
            {L"ぱーせんと", {L"%", L"％"}},
            {L"ぱーみる", {L"‰"}},
            {L"あんぱさんど", {L"&", L"＆"}},

        };
        return d;
    }

    // Map ひらがな/カタカナ char -> its base vowel hiragana. Used to expand
    // a "ー" in the reading to the previous syllable's vowel so users who
    // type "けえき" can hit the same entry as "けーき".
    wchar_t VowelOf(wchar_t c)
    {
        static const std::unordered_map<wchar_t, wchar_t> v = {
            {L'あ',L'あ'},{L'い',L'い'},{L'う',L'う'},{L'え',L'え'},{L'お',L'お'},
            {L'か',L'あ'},{L'き',L'い'},{L'く',L'う'},{L'け',L'え'},{L'こ',L'お'},
            {L'が',L'あ'},{L'ぎ',L'い'},{L'ぐ',L'う'},{L'げ',L'え'},{L'ご',L'お'},
            {L'さ',L'あ'},{L'し',L'い'},{L'す',L'う'},{L'せ',L'え'},{L'そ',L'お'},
            {L'ざ',L'あ'},{L'じ',L'い'},{L'ず',L'う'},{L'ぜ',L'え'},{L'ぞ',L'お'},
            {L'た',L'あ'},{L'ち',L'い'},{L'つ',L'う'},{L'て',L'え'},{L'と',L'お'},
            {L'だ',L'あ'},{L'ぢ',L'い'},{L'づ',L'う'},{L'で',L'え'},{L'ど',L'お'},
            {L'な',L'あ'},{L'に',L'い'},{L'ぬ',L'う'},{L'ね',L'え'},{L'の',L'お'},
            {L'は',L'あ'},{L'ひ',L'い'},{L'ふ',L'う'},{L'へ',L'え'},{L'ほ',L'お'},
            {L'ば',L'あ'},{L'び',L'い'},{L'ぶ',L'う'},{L'べ',L'え'},{L'ぼ',L'お'},
            {L'ぱ',L'あ'},{L'ぴ',L'い'},{L'ぷ',L'う'},{L'ぺ',L'え'},{L'ぽ',L'お'},
            {L'ま',L'あ'},{L'み',L'い'},{L'む',L'う'},{L'め',L'え'},{L'も',L'お'},
            {L'や',L'あ'},{L'ゆ',L'う'},{L'よ',L'お'},
            {L'ら',L'あ'},{L'り',L'い'},{L'る',L'う'},{L'れ',L'え'},{L'ろ',L'お'},
            {L'わ',L'あ'},{L'を',L'お'},
            {L'ゃ',L'あ'},{L'ゅ',L'う'},{L'ょ',L'お'},
        };
        auto it = v.find(c);
        return it != v.end() ? it->second : L'\0';
    }

    std::wstring ExpandChoonpu(std::wstring_view s)
    {
        std::wstring out;
        out.reserve(s.size());
        wchar_t prev = L'\0';
        for (wchar_t c : s)
        {
            if (c == L'ー' && prev != L'\0')
            {
                wchar_t v = VowelOf(prev);
                out.push_back(v ? v : c);
            }
            else
            {
                out.push_back(c);
                prev = c;
            }
        }
        return out;
    }
}

namespace symbols
{
    std::vector<std::wstring> Lookup(std::wstring_view reading)
    {
        if (reading.empty()) return {};
        const auto& d = GetDict();
        auto it = d.find(std::wstring(reading));
        return it != d.end() ? it->second : std::vector<std::wstring>{};
    }

    std::vector<std::wstring> SegmentedLookup(std::wstring_view reading)
    {
        if (reading.empty()) return {};
        const auto& d = GetDict();

        // Longest-prefix match: walk down from full length.
        // Skip single-char segments so 「わいん」 doesn't split into
        // 「わ」(→Σ) + 「いん」(→∈) = 「Σ∈」 and shadow SKK's ワイン.
        // Single-char kana readings that happen to be symbols (わ, ど,
        // に, し, ご, く) still hit via the exact `Lookup` path — this
        // only prevents them from being COMBINED into a longer word.
        for (size_t len = reading.size(); len >= 2; --len)
        {
            std::wstring prefix(reading.substr(0, len));
            auto it = d.find(prefix);
            if (it == d.end()) continue;

            if (len == reading.size())
            {
                return it->second;
            }

            auto rest = SegmentedLookup(reading.substr(len));
            if (rest.empty()) continue;

            // Combine: every prefix candidate × first rest candidate,
            // plus a few extra rest variants paired with the first prefix.
            std::vector<std::wstring> combined;
            combined.reserve(it->second.size() + rest.size());
            const std::wstring& restHead = rest[0];
            for (const auto& p : it->second) combined.push_back(p + restHead);
            for (size_t i = 1; i < rest.size() && i < 6; ++i)
                combined.push_back(it->second[0] + rest[i]);
            return combined;
        }
        return {};
    }

    std::vector<std::wstring> LookupAll(std::wstring_view reading)
    {
        if (auto v = Lookup(reading); !v.empty()) return v;
        if (auto v = LetterVariants(reading); !v.empty()) return v;
        std::wstring expanded = ExpandChoonpu(reading);
        if (expanded != reading)
        {
            if (auto v = Lookup(expanded); !v.empty()) return v;
        }
        if (auto v = SegmentedLookup(reading); !v.empty()) return v;
        if (expanded != reading)
        {
            if (auto v = SegmentedLookup(expanded); !v.empty()) return v;
        }
        return {};
    }

    std::vector<std::wstring> PunctPairs(std::wstring_view typed)
    {
        if (typed.size() != 1) return {};
        wchar_t c = typed[0];

        // Static initializer runs once — the table is tiny so the cost is
        // a few hundred ns the first time and zero after. Each entry lists
        // the typed form FIRST so a bare Enter keeps what the user typed
        // and ↓/Space promotes the alternate form. Add new pairs here
        // when users request them; keep both directions in sync.
        static const std::vector<std::pair<wchar_t, std::vector<std::wstring>>> pairs = {
            { L'！', { L"！", L"!" } },
            { L'!',  { L"!",  L"！" } },
            { L'？', { L"？", L"?" } },
            { L'?',  { L"?",  L"？" } },
            { L'、', { L"、", L"," } },
            { L',',  { L",",  L"、" } },
            { L'。', { L"。", L"." } },
            { L'.',  { L".",  L"。" } },
            { L'＠', { L"＠", L"@" } },
            { L'@',  { L"@",  L"＠" } },
            { L'＃', { L"＃", L"#" } },
            { L'#',  { L"#",  L"＃" } },
            { L'＄', { L"＄", L"$" } },
            { L'$',  { L"$",  L"＄" } },
            { L'％', { L"％", L"%" } },
            { L'%',  { L"%",  L"％" } },
            { L'＆', { L"＆", L"&" } },
            { L'&',  { L"&",  L"＆" } },
            { L'＊', { L"＊", L"*" } },
            { L'*',  { L"*",  L"＊" } },
            // 括弧: 開き側は「ペア」を先頭候補にする。
            // `[`+Space 相当で「」がすぐ確定でき、閉じ括弧の直前に
            // キャレットが落ちる (BracketPairCaretBackShift の対象)。
            // 「」を単独で入れたい場合は Space か ↓ で index 1 の単体形へ。
            { L'（', { L"（）", L"（", L"(" } },
            { L'(',  { L"()",  L"(",  L"（" } },
            { L'）', { L"）", L")" } },
            { L')',  { L")",  L"）" } },
            { L'：', { L"：", L":" } },
            { L':',  { L":",  L"：" } },
            { L'；', { L"；", L";" } },
            { L';',  { L";",  L"；" } },
            { L'「', { L"「」", L"「", L"\"" } },
            { L'」', { L"」", L"\"" } },
            { L'『', { L"『』", L"『" } },
            { L'』', { L"』" } },
            { L'｛', { L"｛｝", L"｛", L"{" } },
            { L'{',  { L"{}",  L"{",  L"｛" } },
            { L'｝', { L"｝", L"}" } },
            { L'}',  { L"}",  L"｝" } },
            { L'＜', { L"＜＞", L"＜", L"<" } },
            { L'<',  { L"<>",  L"<",  L"＜" } },
            { L'＞', { L"＞", L">" } },
            { L'>',  { L">",  L"＞" } },
            { L'［', { L"［］", L"［", L"[" } },
            { L'[',  { L"[]",  L"[",  L"［" } },
            { L'］', { L"］", L"]" } },
            { L']',  { L"]",  L"］" } },
            { L'〔', { L"〔〕", L"〔" } },
            { L'〕', { L"〕" } },
            { L'【', { L"【】", L"【" } },
            { L'】', { L"】" } },
            { L'〈', { L"〈〉", L"〈" } },
            { L'〉', { L"〉" } },
            { L'《', { L"《》", L"《" } },
            { L'》', { L"》" } },
        };
        for (const auto& kv : pairs)
        {
            if (kv.first == c) return kv.second;
        }
        return {};
    }

    std::vector<std::wstring> LetterVariants(std::wstring_view typed)
    {
        if (typed.size() != 1) return {};
        wchar_t c = typed[0];
        bool lower = (c >= L'a' && c <= L'z');
        bool upper = (c >= L'A' && c <= L'Z');
        if (!lower && !upper) return {};

        int idx = lower ? (c - L'a') : (c - L'A');
        wchar_t lo = (wchar_t)(L'a' + idx);
        wchar_t up = (wchar_t)(L'A' + idx);
        std::wstring fwLo(1, (wchar_t)(0xFF41 + idx));  // ｗ
        std::wstring fwUp(1, (wchar_t)(0xFF21 + idx));  // Ｗ
        // REGIONAL INDICATOR SYMBOL LETTER A..Z = U+1F1E6..U+1F1FF,
        // encoded as the surrogate pair D83C DDE6+idx. A lone indicator
        // renders as a boxed letter; two in a row form a flag (🇯🇵).
        std::wstring regional = { (wchar_t)0xD83C, (wchar_t)(0xDDE6 + idx) };
        std::wstring circUp(1, (wchar_t)(0x24B6 + idx)); // Ⓦ
        std::wstring circLo(1, (wchar_t)(0x24D0 + idx)); // ⓦ

        // Typed form first so a bare Enter keeps what the user typed,
        // then its full-width twin, then the opposite case pair.
        std::vector<std::wstring> v;
        if (lower)
            v = { std::wstring(1, lo), fwLo, std::wstring(1, up), fwUp };
        else
            v = { std::wstring(1, up), fwUp, std::wstring(1, lo), fwLo };
        v.push_back(std::move(regional));
        v.push_back(std::move(circUp));
        v.push_back(std::move(circLo));
        return v;
    }

    std::vector<std::wstring> FlagFromIso2(std::wstring_view typed)
    {
        if (typed.size() != 2) return {};
        auto toIdx = [](wchar_t c) -> int {
            if (c >= L'a' && c <= L'z') return (int)(c - L'a');
            if (c >= L'A' && c <= L'Z') return (int)(c - L'A');
            return -1;
        };
        int a = toIdx(typed[0]);
        int b = toIdx(typed[1]);
        if (a < 0 || b < 0) return {};
        // Same encoding as LetterVariants' single-indicator path — the two
        // indicators sit next to each other in the returned wstring so a
        // flag-aware font/shaper collapses them into one flag glyph.
        std::wstring flag = {
            (wchar_t)0xD83C, (wchar_t)(0xDDE6 + a),
            (wchar_t)0xD83C, (wchar_t)(0xDDE6 + b),
        };
        return { std::move(flag) };
    }

    std::vector<std::wstring> AsciiWidthCaseVariants(std::wstring_view typed)
    {
        if (typed.empty()) return {};

        // Every char must be a width-convertible letter or digit, and there
        // must be at least one letter (a pure-digit run already has its own
        // number path and shouldn't be hijacked here).
        bool hasLetter = false;
        for (wchar_t c : typed)
        {
            bool asciiUp   = (c >= L'A' && c <= L'Z');
            bool asciiLo   = (c >= L'a' && c <= L'z');
            bool fwUp      = (c >= 0xFF21 && c <= 0xFF3A);
            bool fwLo      = (c >= 0xFF41 && c <= 0xFF5A);
            bool asciiDig  = (c >= L'0' && c <= L'9');
            bool fwDig     = (c >= 0xFF10 && c <= 0xFF19);
            if (asciiUp || asciiLo || fwUp || fwLo) hasLetter = true;
            else if (!asciiDig && !fwDig) return {};
        }
        if (!hasLetter) return {};

        bool typedFull = false;
        for (wchar_t c : typed)
        {
            if ((c >= 0xFF21 && c <= 0xFF3A) || (c >= 0xFF41 && c <= 0xFF5A) ||
                (c >= 0xFF10 && c <= 0xFF19)) { typedFull = true; break; }
        }

        // caseMode: 0 = preserve each char's case, 1 = force lower, 2 = force upper.
        auto render = [&](bool full, int caseMode) {
            std::wstring out;
            out.reserve(typed.size());
            for (wchar_t c : typed)
            {
                bool isUpper = false; int idx = 0; bool letter = false; int dig = -1;
                if      (c >= L'A' && c <= L'Z')       { letter = true; isUpper = true;  idx = c - L'A'; }
                else if (c >= L'a' && c <= L'z')       { letter = true; isUpper = false; idx = c - L'a'; }
                else if (c >= 0xFF21 && c <= 0xFF3A)   { letter = true; isUpper = true;  idx = c - 0xFF21; }
                else if (c >= 0xFF41 && c <= 0xFF5A)   { letter = true; isUpper = false; idx = c - 0xFF41; }
                else if (c >= L'0' && c <= L'9')       { dig = c - L'0'; }
                else                                   { dig = c - 0xFF10; }

                if (letter)
                {
                    bool up = (caseMode == 0) ? isUpper : (caseMode == 2);
                    if (full) out.push_back((wchar_t)((up ? 0xFF21 : 0xFF41) + idx));
                    else      out.push_back((wchar_t)((up ? L'A' : L'a') + idx));
                }
                else
                {
                    if (full) out.push_back((wchar_t)(0xFF10 + dig));
                    else      out.push_back((wchar_t)(L'0' + dig));
                }
            }
            return out;
        };

        std::vector<std::wstring> v;
        auto add = [&](std::wstring s) {
            if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(std::move(s));
        };
        add(std::wstring(typed));   // index 0 = typed form (bare Enter keeps it)
        add(render(!typedFull, 0)); // opposite width, same case (most common swap)
        add(render(false, 2));      // half-width upper
        add(render(false, 1));      // half-width lower
        add(render(true, 2));       // full-width upper
        add(render(true, 1));       // full-width lower
        return v;
    }

    std::vector<std::wstring> AcronymExpansions(std::wstring_view key)
    {
        // Single letters are LetterVariants' job; acronyms are 2+ chars.
        if (key.size() < 2) return {};

        // Curated built-in acronym table. Value order is {日本語訳, 英語フル
        // スペル} so a key like「IMF」yields「国際通貨基金 / International
        // Monetary Fund」in that order behind the width/case forms. Entries
        // with no natural Japanese name list the English spell-out only.
        // Multi-sense acronyms keep the most common expansion. Extend freely;
        // the LLM fallback covers whatever isn't listed here.
        static const std::unordered_map<std::wstring, std::vector<std::wstring>> table = {
            // 国際機関・経済
            { L"IMF",    { L"国際通貨基金", L"International Monetary Fund" } },
            { L"WHO",    { L"世界保健機関", L"World Health Organization" } },
            { L"UN",     { L"国際連合", L"United Nations" } },
            { L"WTO",    { L"世界貿易機関", L"World Trade Organization" } },
            { L"NATO",   { L"北大西洋条約機構", L"North Atlantic Treaty Organization" } },
            { L"EU",     { L"欧州連合", L"European Union" } },
            { L"ASEAN",  { L"東南アジア諸国連合", L"Association of Southeast Asian Nations" } },
            { L"OPEC",   { L"石油輸出国機構", L"Organization of the Petroleum Exporting Countries" } },
            { L"UNESCO", { L"国連教育科学文化機関", L"United Nations Educational, Scientific and Cultural Organization" } },
            { L"UNICEF", { L"国連児童基金", L"United Nations Children's Fund" } },
            { L"OECD",   { L"経済協力開発機構", L"Organisation for Economic Co-operation and Development" } },
            { L"APEC",   { L"アジア太平洋経済協力", L"Asia-Pacific Economic Cooperation" } },
            { L"GATT",   { L"関税及び貿易に関する一般協定", L"General Agreement on Tariffs and Trade" } },
            { L"FTA",    { L"自由貿易協定", L"Free Trade Agreement" } },
            { L"TPP",    { L"環太平洋パートナーシップ協定", L"Trans-Pacific Partnership" } },
            { L"GDP",    { L"国内総生産", L"Gross Domestic Product" } },
            { L"GNP",    { L"国民総生産", L"Gross National Product" } },
            { L"NGO",    { L"非政府組織", L"Non-Governmental Organization" } },
            { L"NPO",    { L"非営利組織", L"Nonprofit Organization" } },
            // 政府・機関
            { L"NASA",   { L"アメリカ航空宇宙局", L"National Aeronautics and Space Administration" } },
            { L"FBI",    { L"連邦捜査局", L"Federal Bureau of Investigation" } },
            { L"CIA",    { L"中央情報局", L"Central Intelligence Agency" } },
            // IT・技術
            { L"AI",     { L"人工知能", L"Artificial Intelligence" } },
            { L"API",    { L"Application Programming Interface" } },
            { L"CPU",    { L"中央処理装置", L"Central Processing Unit" } },
            { L"GPU",    { L"画像処理装置", L"Graphics Processing Unit" } },
            { L"OS",     { L"基本ソフト", L"Operating System" } },
            { L"PC",     { L"パソコン", L"Personal Computer" } },
            { L"USB",    { L"Universal Serial Bus" } },
            { L"URL",    { L"Uniform Resource Locator" } },
            { L"HTML",   { L"HyperText Markup Language" } },
            { L"HTTP",   { L"HyperText Transfer Protocol" } },
            { L"HTTPS",  { L"HyperText Transfer Protocol Secure" } },
            { L"DNA",    { L"デオキシリボ核酸", L"Deoxyribonucleic Acid" } },
            { L"ATM",    { L"現金自動預払機", L"Automated Teller Machine" } },
            { L"GPS",    { L"全地球測位システム", L"Global Positioning System" } },
            { L"LED",    { L"発光ダイオード", L"Light Emitting Diode" } },
            { L"PDF",    { L"Portable Document Format" } },
            { L"VPN",    { L"仮想プライベートネットワーク", L"Virtual Private Network" } },
            { L"RAM",    { L"Random Access Memory" } },
            { L"ROM",    { L"Read Only Memory" } },
            { L"SSD",    { L"Solid State Drive" } },
            { L"HDD",    { L"Hard Disk Drive" } },
            { L"IOT",    { L"モノのインターネット", L"Internet of Things" } },
            { L"DX",     { L"デジタルトランスフォーメーション", L"Digital Transformation" } },
            { L"CMS",    { L"コンテンツ管理システム", L"Content Management System" } },
            { L"CRM",    { L"顧客関係管理", L"Customer Relationship Management" } },
            { L"ERP",    { L"企業資源計画", L"Enterprise Resource Planning" } },
            { L"KPI",    { L"重要業績評価指標", L"Key Performance Indicator" } },
            { L"KGI",    { L"重要目標達成指標", L"Key Goal Indicator" } },
            { L"OKR",    { L"目標と主要な結果", L"Objectives and Key Results" } },
            { L"NDA",    { L"秘密保持契約", L"Non-Disclosure Agreement" } },
            // 役職・ビジネス
            { L"CEO",    { L"最高経営責任者", L"Chief Executive Officer" } },
            { L"CFO",    { L"最高財務責任者", L"Chief Financial Officer" } },
            { L"COO",    { L"最高執行責任者", L"Chief Operating Officer" } },
            { L"CTO",    { L"最高技術責任者", L"Chief Technology Officer" } },
            { L"CIO",    { L"最高情報責任者", L"Chief Information Officer" } },
            { L"CMO",    { L"最高マーケティング責任者", L"Chief Marketing Officer" } },
            { L"CPA",    { L"公認会計士", L"Certified Public Accountant" } },
            { L"HR",     { L"人事", L"Human Resources" } },
            { L"HQ",     { L"本社", L"Headquarters" } },
            { L"GM",     { L"本部長", L"General Manager" } },
            { L"VP",     { L"副社長", L"Vice President" } },
            { L"PM",     { L"プロジェクトマネージャー", L"Project Manager" } },
            { L"PR",     { L"広報", L"Public Relations" } },
            { L"QC",     { L"品質管理", L"Quality Control" } },
            { L"RFP",    { L"提案依頼書", L"Request For Proposal" } },
            { L"ROI",    { L"投資収益率", L"Return On Investment" } },
            { L"ROA",    { L"総資産利益率", L"Return On Assets" } },
            { L"ROE",    { L"自己資本利益率", L"Return On Equity" } },
            { L"EPS",    { L"一株当たり利益", L"Earnings Per Share" } },
            { L"PER",    { L"株価収益率", L"Price Earnings Ratio" } },
            { L"BS",     { L"貸借対照表", L"Balance Sheet" } },
            { L"SEO",    { L"検索エンジン最適化", L"Search Engine Optimization" } },
            { L"IT",     { L"情報技術", L"Information Technology" } },
            { L"MTG",    { L"会議", L"Meeting" } },
            { L"FIFO",   { L"先入先出", L"First In, First Out" } },
            { L"LIFO",   { L"後入先出", L"Last In, First Out" } },
            { L"B2B",    { L"企業間取引", L"Business to Business" } },
            { L"B2C",    { L"企業対消費者取引", L"Business to Consumer" } },
            // 定型表現・スラング
            { L"ASAP",   { L"できるだけ早く", L"As Soon As Possible" } },
            { L"FYI",    { L"参考まで", L"For Your Information" } },
            { L"FAQ",    { L"よくある質問", L"Frequently Asked Questions" } },
            { L"ETA",    { L"到着予定時刻", L"Estimated Time of Arrival" } },
            { L"TBD",    { L"要決定", L"To Be Decided" } },
            { L"TBA",    { L"未定", L"To Be Announced" } },
            { L"TBC",    { L"要確認", L"To Be Confirmed" } },
            { L"BTW",    { L"ところで", L"By The Way" } },
            { L"IMO",    { L"私の意見では", L"In My Opinion" } },
            { L"IDK",    { L"わかりません", L"I Don't Know" } },
            { L"TMI",    { L"情報が多すぎ", L"Too Much Information" } },
            { L"TBH",    { L"正直に言うと", L"To Be Honest" } },
            { L"BRB",    { L"すぐ戻ります", L"Be Right Back" } },
            { L"POV",    { L"視点", L"Point Of View" } },
            { L"AKA",    { L"別名", L"Also Known As" } },
            { L"DIY",    { L"日曜大工", L"Do It Yourself" } },
            { L"WFH",    { L"在宅勤務", L"Working From Home" } },
            { L"PTO",    { L"有給休暇", L"Paid Time Off" } },
        };

        auto it = table.find(std::wstring(key));
        if (it != table.end()) return it->second;
        return {};
    }
}
