#include <Arduino.h>
#include <unity.h>

const int MAX_HISTORY = 3;
String chatHistory[MAX_HISTORY];
int historyIndex = 0;
int totalMessages = 0;

void addMessage(String msg) {
    chatHistory[historyIndex] = msg;
    historyIndex = (historyIndex + 1) % MAX_HISTORY;
    if(totalMessages < MAX_HISTORY) totalMessages++;
}

void setUp(void) {

    historyIndex = 0;
    totalMessages = 0;
    
}

void tearDown(void) {

}

void test_message_counter(void) {
    addMessage("Hello");
    TEST_ASSERT_EQUAL_INT(1, totalMessages);
}

void test_circular_buffer(void) {
    addMessage("Msg 1");
    addMessage("Msg 2");
    addMessage("Msg 3");
    addMessage("Msg 4");

    TEST_ASSERT_EQUAL_INT(3, totalMessages); 

    TEST_ASSERT_EQUAL_STRING("Msg 4", chatHistory[0].c_str()); 
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    
    RUN_TEST(test_message_counter);
    RUN_TEST(test_circular_buffer);
    
    UNITY_END();
}

void loop() {}