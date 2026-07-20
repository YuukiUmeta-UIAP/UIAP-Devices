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