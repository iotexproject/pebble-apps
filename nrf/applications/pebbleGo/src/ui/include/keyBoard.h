#ifndef     __KEY_BOARD_H__
#define     __KEY_BOARD_H__


#define IO_UP_KEY     28    //p0.28  0--key press 
#define IO_DOWN_KEY   16    //p0.16  0--key press

enum USER_KEY_DEF{
    KB_NO_KEY=0,
    KB_POWER_KEY=(1<<0),
    KB_UP_KEY=(1<<1),
    KB_DOWN_KEY=(1<<2),
    KB_OTA_UPDATE_KEY=(KB_POWER_KEY|KB_UP_KEY),
};


void ClearKey(void);
uint8_t getKey(void);
void iotex_key_init(void);
bool IsEnterPressed(void);



#endif
