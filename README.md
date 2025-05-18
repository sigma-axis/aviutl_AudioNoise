# 音声ノイズ生成 AviUtl 拡張編集フィルタプラグイン

AviUtl の拡張編集に「音声ノイズ」「音声ノイズ乗算」「パルスノイズ」のフィルタ効果・フィルタオブジェクトを追加するプラグインです．[ホワイトノイズ](https://ja.wikipedia.org/wiki/ホワイトノイズ)や[ブラウンノイズ](https://ja.wikipedia.org/wiki/ブラウニアンノイズ)などを再生したり，他の音声にノイズを混ぜたような効果を乗せたりできます．

[ダウンロードはこちら．](https://github.com/sigma-axis/aviutl_AudioNoise/releases) [紹介動画．](https://www.nicovideo.jp/watch/sm44997398)

![風が吹き荒れてるっぽいノイズ波形](https://github.com/user-attachments/assets/fbdba93f-4331-451a-b99d-adf6397b342c)

https://github.com/user-attachments/assets/b59be335-5928-4829-94a3-7cbc44afb263

##  動作要件

- AviUtl 1.10 + 拡張編集 0.92

  http://spring-fragrance.mints.ne.jp/aviutl
  - 拡張編集 0.93rc1 等の他バージョンでは動作しません．

- Visual C++ 再頒布可能パッケージ（\[2015/2017/2019/2022\] の x86 対応版が必要）

  https://learn.microsoft.com/ja-jp/cpp/windows/latest-supported-vc-redist

- patch.aul の `r43 謎さうなフォーク版58` (`r43_ss_58`) 以降

  https://github.com/nazonoSAUNA/patch.aul/releases/latest


##  導入方法

`aviutl.exe` と同階層にある `plugins` フォルダ内に `AudioNoise.eef` ファイルをコピーしてください．


##  使い方

音声系オブジェクトのフィルタ効果の追加メニューやメディアオブジェクト，フィルタオブジェクトの追加メニューから「音声ノイズ」「音声ノイズ乗算」「パルスノイズ」を選んで使用します．

### 音声ノイズ

ホワイトノイズやブラウンノイズなどを含めた，一般に[べき乗則ノイズ](https://ja.wikipedia.org/wiki/カラードノイズ#べき乗則ノイズ)と呼ばれるノイズを生成し，音として再生します．

![音声ノイズの波形の例](https://github.com/user-attachments/assets/8c208be5-a033-4396-909a-354f9096ae18)

![音声ノイズのGUI](https://github.com/user-attachments/assets/d16db76d-083c-4dba-b80d-9804c7ea5305)

音声の入力フィルタとしてタイムラインに配置します．

####  `指数`

ノイズのパワースペクトル分布が，周波数を $f$ として $1/f^\alpha$ に比例するように生成します．`指数` はこのときの $\alpha$ を，その 100 倍の値で指定します．

この値を大きくすると低音成分が強調され，高音成分が抑えられます．逆に小さくすると高音成分が強調され，低音成分が抑えられます．

主なノイズの種類と指定値は以下の通り:

- `指数` = `-200` :arrow_right: **パープルノイズ**

  ![パープルノイズの波形の例](https://github.com/user-attachments/assets/3b8877e5-ff75-47eb-93b9-e9669c054afb)

  - ホワイトノイズの微分として表現できる．

- `指数` = `-100` :arrow_right: **ブルーノイズ**

  ![ブルーノイズの波形の例](https://github.com/user-attachments/assets/648ac042-30c2-424b-a750-6aabb23a5ba4)

  - ピンクノイズの微分として表現できる．

- `指数` = `0` :arrow_right: **ホワイトノイズ**

  ![ホワイトノイズの波形の例](https://github.com/user-attachments/assets/f2a4b10b-3754-4c40-bb76-c7950d3a4962)

  - パワースペクトルの確率分布は周波数によらず一定．

- `指数` = `+100` :arrow_right: **ピンクノイズ**

  ![ピンクノイズの波形の例](https://github.com/user-attachments/assets/689cda0f-0296-4bd3-9cb2-b04b092cc0f3)

  - スペクトル分布をオクターブ単位で区切った総エネルギー量が周波数帯によらず一定．
  - 広義には $0 < \alpha < 2$ な範囲のノイズを指すこともある．
  - 一時期「 $1/f$ ゆらぎ」として流行ったことがある．

- `指数` = `+200` :arrow_right: **ブラウンノイズ**

  ![ブラウンノイズの波形の例](https://github.com/user-attachments/assets/5845fd4e-560c-4c0b-8936-2f345164ef2d)

  - ホワイトノイズの積分として表現できる．
  - レッドノイズと呼ばれることもある．
  - 名前の由来は色ではなく[人名](https://ja.wikipedia.org/wiki/ロバート・ブラウン)．

最小値は `-400.00`, 最大値は `400.00`, 初期値は `0.00`.

####  `分解能`

ノイズ音声のサンプルレートを操作します．440 Hz の**ラ**を基準とした半音単位の数値で指定します．サンプルレートを粗くすると昔の電子音のような雰囲気が出ます．

最小値は `-48.00`, 最大値・初期値は `96.00`.
- 最大値の場合はプロジェクトファイルの音声のサンプルレートと一致します．この場合，シーンの再生速度などの影響も受けません．

####  `背景音量`

このオブジェクトより上のレイヤーに置かれた音声の音量を操作します．音量を % 単位で指定します．ノイズでセリフの音消しをするなどの表現に利用できます．

最小値は `0.0`, 最大値は `200.0`, 初期値は `100.0`.

####  `ステレオ`

ノイズを左右で異なるシードで生成します．

初期値はチェックなし．

####  `補間する`

[`分解能`](#分解能) が低い場合，ノイズの2つのサンプル間を線形補間します．こもったような音に聞こえるようになります．

初期値はチェックなし．

####  `シード`

ノイズ生成に使うシード値を指定します．

- `0` 以上のシードだと，同じシード値でもオブジェクトによって異なるノイズが生成されます．
- 負のシードだと同じシード値ならオブジェクトが違っても全く同じノイズが生成されます．

最小値は `-2147483648`, 最大値は `2147483647`, 初期値は `0`.

`設定...` ボタンで表示されるダイアログで入力できます．

####  `FFTサイズ`

ノイズ生成の計算過程で使用する [FFT](https://ja.wikipedia.org/wiki/高速フーリエ変換) のサイズを指定します．小さいと計算は高速になりますが，低い周波数成分の精度が低くなります．

指定可能な数値は以下の通り:
- `512`.
- `1024`.
- `2048` (初期値).
- `4096`.
- `8192`.

`設定...` ボタンで表示されるダイアログで入力できます．

### 音声ノイズ乗算

ノイズ波形を既存の音声に乗算します．再生中の音声の音量に応じたノイズを乗せられます．

![音声ノイズ乗算の適用例](https://github.com/user-attachments/assets/b8af895c-a202-40aa-ba63-f844b3e7fe41)

![音声ノイズ乗算のGUI](https://github.com/user-attachments/assets/d1547041-8627-45fa-aff2-928a3ee8ae44)

音声のフィルタ効果として音声系オブジェクトに追加します．あるいは，音声のフィルタオブジェクトとしてタイムラインに配置します．

####  `指数`, `分解能`, `ステレオ`, `補間する`, `シード`, `FFTサイズ`

[音声ノイズ](#音声ノイズ)と同様の設定項目で，生成ノイズの特性を指定します．

####  `強さ`

このフィルタ効果による影響の度合いを % 単位で指定します．`0` だとこのフィルタ効果を無効化した場合と同じ結果になります．

最小値は `0.0`, 最大値・初期値は `100.0`.

####  `頭打ちdB`

ノイズの波形がこの指定値を超えたサンプルは，元音声に対して `1.0` を乗算するようになります（つまり，変化なし）．`1.0` 倍で頭打ちにするしきい値を，内部基準値からの相対 dB (デシベル) 単位で指定します．

- `頭打ちdB` を小さくすると，このフィルタによる影響を受けない音声サンプルが増えます．
  - 特に [`指数`](#指数) が大きく `頭打ちdB` が小さい場合は，フィルタなしと同じように聞こえる時間が長くなります．
- [`足切りdB`](#足切りdb) を下回った場合は，`足切りdB` と同じ値を指定した場合と同等の挙動になります．
- [`ON/OFF反転`](#onoff反転) が ON の場合は，ノイズがこの数値を超えた部分は無音になります．

最小値は `-72.00`, 最大値は `24.00`, 初期値は `0.00`.

####  `足切りdB`

ノイズの波形がこの指定値を下回ったサンプルは，元音声に対して `0.0` を乗算するようになります（つまり，無音化）．`0.0` 倍で足切りにするしきい値を，内部基準値からの相対 dB (デシベル) 単位で指定します．

- `足切りdB` を大きくすると，無音な音声サンプルが増えます．
  - 特に [`指数`](#指数) も大きい場合は，時々しか音が鳴らなくなります．
- [`ON/OFF反転`](#onoff反転) が ON の場合は，ノイズがこの数値を下回った部分は無加工（変化なし）になります．

最小値は `-72.00`, 最大値は `24.00`, 初期値は `-72.00`.

####  `ON/OFF反転`

ノイズによる乗算値の `0.0` -- `1.0` を反転します．

無音化されていた音声サンプルが無加工になり，逆に無加工だった音声サンプルが無音化するようになります．時々にしか音が鳴らないように調整したものを，時々音が途切れるような挙動に変えられます．

初期値はチェックなし．

### パルスノイズ

ごく短い矩形波（いわゆる「プツプツノイズ」）を 1 つ生成します．発生位置や矩形の長さをサブフレーム単位で細かく調整できます．また [`背景音量`](#背景音量-1) の指定はこの矩形波の存在範囲のみ有効なので，サブフレーム単位で細かい音声部分を無音化できます．

![パルスノイズの波形の例](https://github.com/user-attachments/assets/fe073c46-bd01-47ff-8d15-1c6befcdf2dc)

![パルスノイズのGUI](https://github.com/user-attachments/assets/5549240a-7286-4f24-9f97-e53b31e3acc1)

音声の入力フィルタとしてタイムラインに配置します．

####  `位置(ms)`

ノイズの発生位置を，オブジェクト冒頭からのミリ秒 ($\tfrac{1}{1000}$ 秒) 単位で指定します．

最小値・初期値は `0.00`, 最大値は `500.00` (0.5 秒).

####  `幅(ms)`

矩形波の幅（存在時間）を，ノイズ発生時からのミリ秒 ($\tfrac{1}{1000}$ 秒) 単位で指定します．音声サンプル 1 つ分は最低保障の長さとして再生されます．

最小値・初期値は `0.00`, 最大値は `200.00` (0.2 秒).

####  `背景音量`

このオブジェクトより上のレイヤーに置かれた音声の音量を操作します．音量を % 単位で指定します．ノイズでセリフの音消しをするなどの表現に利用できます．

[音声ノイズの `背景音量`](#背景音量) とは異なり，矩形波の存在範囲のみが音量調整の対象です．

最小値は `0.0`, 最大値は `200.0`, 初期値は `100.0`.

##  既知の問題

- [`指数`](#指数) が大きい場合，[`FFTサイズ`](#fftサイズ) の大きくするとノイズ音が小さく聞こえるようになります．本来なら同じ大きさで聞こえるように調整したかったのですが，無理に調整しようとすると音割れ等が起こりやすくなってしまったこともあり，現状維持の方針にしています．

##  謝辞

元々ノイズ生成の機能は[単純音生成](https://github.com/nazonoSAUNA/simple_wave.eef)の追加機能として，私がプルリクエストを出したものでした．その際にノイズ生成の新しい `.eef` 作成の提案を受け，機能のいくつかはその際のやり取りが元になっています．この場で恐縮ですが，プラグイン作成のきっかけになったさうなさうな様に感謝申し上げます．  

##  改版履歴

- **v1.00** (2025-05-18)

  - 初版．


##  ライセンス

このプログラムの利用・改変・再頒布等に関しては MIT ライセンスに従うものとします．

---

The MIT License (MIT)

Copyright (C) 2025 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

https://mit-license.org/


# Credits

##  aviutl_exedit_sdk

https://github.com/ePi5131/aviutl_exedit_sdk （利用したブランチは[こちら](https://github.com/sigma-axis/aviutl_exedit_sdk/tree/self-use)です．）

---

1条項BSD

Copyright (c) 2022
ePi All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
THIS SOFTWARE IS PROVIDED BY ePi “AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ePi BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


# 連絡・バグ報告

- GitHub: https://github.com/sigma-axis
- Twitter: https://x.com/sigma_axis
- nicovideo: https://www.nicovideo.jp/user/51492481
- Misskey.io: https://misskey.io/@sigma_axis
- Bluesky: https://bsky.app/profile/sigma-axis.bsky.social

