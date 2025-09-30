# OADG 109A(JIS配列)互換40%分割キーボード

工作する人向け(主に自分向け)に作りました。一般的な分割キーボードは左右を有線ケーブルで繋げ、PCへ接続されるUSBコネクタは中央付近にあります。せっかく分割するなら分割した空間を工作に利用したいと考え、左右を繋げず、USBコネクタを左右端にしました。

<img src=".src/img/PXL_20250213_150136355.jpg" alt="分割状態" style="width:32%">
<img src=".src/img/PXL_20250213_150242515.jpg" alt="左右くっつけ状態" style="width:32%">
<img src=".src/img/PXL_20250213_150404449.jpg" alt="マイコンボードのマウント" style="width:32%">

# Status / ステータス

- バージョン1.1です。
- Windows 10/11、Ubuntu 22.04/24.04で動作確認できています。
- コストはおよそ10,000~15,000円/セットです。
- 不具合はなく、完成度は及第点ぐらいです。

# Circuit / 回路

[<img src=".src/doc/left.png" alt="左回路図" style="width:32%">](.src/doc/left.pdf) [<img src=".src/doc/right.png" alt="右回路図" style="width:32%">](.src/doc/right.pdf)  
[設計データ(Eagle)とガーバデータ(JLCPCB)](.src/doc/Keyboard-V1.1.zip)

## 部品購入先

- https://www.switch-science.com/products/9914
- https://www.switch-science.com/products/3763?variant=42381993312454
- https://shop.yushakobo.jp/products/7050?variant=46450806882535
- https://shop.yushakobo.jp/products/dsa-blank-keycaps?variant=37665598603425
- https://shop.yushakobo.jp/products/10263?variant=50496233799911
- https://web.archive.org/web/20250325213206/https://talpkeyboard.net/items/5f9428f99a06e5313541cda7
- https://www.marutsu.co.jp/pc/i/833930/

# Program / プログラム

https://github.com/YuukiUmeta-UIAP/rv003usb/tree/custom/main/demo_pikokey_hid

# 開発過程

1. [IKeJI](https://www.ikejima.org/)さんから[Let's split fes: レツプリ祭り](https://www.ikejima.org/projects/2024032-lets-split2.html)の基板をもらったので、改造して動作確認

    <img src=".src/img/PXL_20250120_132242160.jpg" alt="改造1" style="width:32%"> <img src=".src/img/PXL_20250120_132301329.jpg" alt="改造2" style="width:32%"> <img src=".src/img/PXL_20250121_151223214.jpg" alt="改造3" style="width:32%">  
    <img src=".src/img/PXL_20250121_151257683.jpg" alt="改造詳細1" style="width:32%"> <img src=".src/img/PXL_20250505_040332651.jpg" alt="改造詳細2" style="width:32%"> <img src=".src/img/PXL_20250122_151522777.jpg" alt="改造後の動作確認" style="width:32%">  

2. [Self-Made Keyboards in Japan](https://scrapbox.io/self-made-kbds-ja/)のDiscordサーバでIKeJIさんとデバッグ

3. 動作確認できたので基板設計して[JLCPCB](https://jlcpcb.com/)に基板製造とPCBAを発注

    <img src=".src/img/PXL_20250211_135419008.jpg" alt="基板表面" style="width:32%"> <img src=".src/img/PXL_20250211_135333457.jpg" alt="基板裏面" style="width:32%">

4. リード部品を はんだ して、ゴム足を付けたら完成！

    <img src=".src/img/PXL_20250213_150555474.jpg" alt="左基板実装後の裏面" style="width:32%"> <img src=".src/img/PXL_20250213_150458010.jpg" alt="右基板実装後の裏面" style="width:32%">

# 感想・まとめ

人に見せても恥ずかしくないキーボードになりましたが、設計者だからこそ気になる不満点があります。次の試作で改善する予定です。

- キースイッチのフットプリントがラフで、キースイッチの実装場所がバラついている点
- キースイッチ2つ使ったキーの押し込みが渋い点(バラついたままキーキャップがはめ込まれているため)
- 同時にインジケータを付けたかった点(デバッグ用の引き出しコネクタのみ)
- 左右を合わせた時にロック機構で一体キーボードになるようにすべきだった点
- せっかくなら光らせればよかったと思う点

初の自作キーボードでしたが、とても楽しく作ることができました。また、[IKeJI](https://www.ikejima.org/)さんのフォローのおかげででとても簡単に作れました。特にキーボードとして振る舞う通信部分とキーマトリクスの説明を細かくしてくれました。改めて感謝申し上げます。