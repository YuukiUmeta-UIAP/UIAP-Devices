int A = 30;

void setup() {
  Config.X = 0x00400000; 
  Config.Y = 0x000000FF; 
}

void loop() {
  // A = 10;
  A = 20;
}

int sub(int x) {
  return x + 50;
}