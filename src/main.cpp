#include <ReaderApp.h>

static reader::ReaderApp reader_app;

void setup() { reader_app.begin(); }
void loop() { reader_app.service(); }
