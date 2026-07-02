#include "symboldictionary.h"
#include <unordered_map>

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
            {L"0", {L"０", L"Ⅹ", L"⓪", L"〇", L"零"}},
            {L"1", {L"１", L"Ⅰ", L"ⅰ", L"①", L"❶", L"一", L"壱"}},
            {L"2", {L"２", L"Ⅱ", L"ⅱ", L"②", L"❷", L"二", L"弐"}},
            {L"3", {L"３", L"Ⅲ", L"ⅲ", L"③", L"❸", L"三", L"参"}},
            {L"4", {L"４", L"Ⅳ", L"ⅳ", L"④", L"❹", L"四", L"肆"}},
            {L"5", {L"５", L"Ⅴ", L"ⅴ", L"⑤", L"❺", L"五", L"伍"}},
            {L"6", {L"６", L"Ⅵ", L"ⅵ", L"⑥", L"❻", L"六", L"陸"}},
            {L"7", {L"７", L"Ⅶ", L"ⅶ", L"⑦", L"❼", L"七", L"漆"}},
            {L"8", {L"８", L"Ⅷ", L"ⅷ", L"⑧", L"❽", L"八", L"捌"}},
            {L"9", {L"９", L"Ⅸ", L"ⅸ", L"⑨", L"❾", L"九", L"玖"}},
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
            { L'（', { L"（", L"(" } },
            { L'(',  { L"(",  L"（" } },
            { L'）', { L"）", L")" } },
            { L')',  { L")",  L"）" } },
            { L'：', { L"：", L":" } },
            { L':',  { L":",  L"：" } },
            { L'；', { L"；", L";" } },
            { L';',  { L";",  L"；" } },
            { L'「', { L"「", L"\"" } },
            { L'」', { L"」", L"\"" } },
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
}
