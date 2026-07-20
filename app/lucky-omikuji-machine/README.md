# Lucky Omikuji Machine / ラッキーおみくじマシーン

超音波センサとサーボモーターを使ったおみくじマシーン。
おじぎをすると超音波センサが検知し、サーボモーターの針が2往復した後、ランダムな角度で止まって「今日の運勢」を指す。

UIAPruby(Rubyライクな文法でマイコンを制御できるDSL)で書いたスクリプトを、ブラウザ上で軽量バイトコード(.urb)へコンパイルし、UIAPduino(CH32V003)上で動くTinyVMがそのバイトコードを実行することで実現している。マイコン上でRuby VMそのものを動かしているわけではない。

![App image / 作品の写真](.src/img/おみくじ.jpg)

<video src="./.src/ラッキーおみくじ.mp4"></video>

紹介スライド: [数百円からしめるRuby電子工作](https://speakerdeck.com/tarosay/shu-bai-yuan-karashi-merurubydian-zi-gong-zuo?slide=8)

# Status / ステータス

- 実機で動作確認済み
- UIAPruby(Ruby風DSL)で書いたスクリプトをブラウザでバイトコード(.urb)にコンパイルし、TinyVM上で実行する方式

# 材料

- UIAPduino Pro Micro(CH32V003)
- 超音波センサ HC-SR04
- サーボモーター
- SDカードアダプタ + SDカード

# Circuit / 回路

回路図の代わりに実体配線の写真を掲載する。

![Circuit photo / 回路写真](.src/img/回路写真.jpg)

# Program / プログラム

このアプリは2つのパートで構成される。

1. **TinyVMランナー(firmware)**: UIAPrubyが生成したCH32V003用のスケッチ。バイトコード(.urb)を実行するVMで、Rubyそのものは解釈しない。[firmware/UIAPrubyVmQ1PwUsRnVe.ino](firmware/UIAPrubyVmQ1PwUsRnVe.ino) をUIAPduinoへ書き込む。
2. **アプリ本体(Rubyスクリプト)**: [omikuji.rb](omikuji.rb)。[UIAPruby Web](https://tarosay.github.io/uiap-hid-web/uiapruby.html) 上で `@ruby/prism`(WASM)により解析され、バイトコード(.urb)へコンパイルされたのち、SDカードへ転送されTinyVMで実行される。

## 書き込み手順

1. Arduino IDEの「追加のボードマネージャのURL」に以下を追加する。

   ```
   https://raw.githubusercontent.com/tarosay/board_manager_files/main/package_uiap_hid_index.json
   ```

2. ボードマネージャから「UIAPduino HID」パッケージをインストールする。
3. ボードとして `UIAP_HID:ch32v:CH32V003:pnum=V14,usb=webhid,opt=oslto`([firmware/sketch.yaml](firmware/sketch.yaml)参照)を選択し、[firmware/UIAPrubyVmQ1PwUsRnVe.ino](firmware/UIAPrubyVmQ1PwUsRnVe.ino)をビルド・書き込みする。
4. WebHID対応ブラウザ(Chromeなど)で [UIAPruby Web](https://tarosay.github.io/uiap-hid-web/uiapruby.html) を開き、[omikuji.rb](omikuji.rb) を読み込ませてバイトコード(.urb)へコンパイルし、USBで接続したデバイスのSDカードへ転送する。このページを使わないと`.rb`をデバイスが実行できる形式(.urb)に変換できない。

## omikuji.rb

```ruby
led = GPIO.new 2, GPIO::OUT
sonar = Ultrasonic.new 3, 4  # 第1引数=TRIG(pin3), 第2引数=ECHO(pin4)
servo = PWM.new 5 
servo.frequency 50   # サーボ用の基準周波数（周期 2ms）
srand  # シード初期化（起動ごとに違うパターンになる）
servo.angle 83

loop do
  d = sonar.read
  warn d
  if d < 30 && d > 10
    led.on
    2.times do
      servo.angle 40
      sleep 1.0
      servo.angle 115 
      sleep 1.0
    end
    n = rand 40..115 
    servo.angle n
  #warn n
  else
    led.off
  end
  wait_ms 200
end
```
