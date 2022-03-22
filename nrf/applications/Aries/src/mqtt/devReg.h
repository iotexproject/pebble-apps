#ifndef __DEV_REG_H__
#define __DEV_REG_H__

#define  DEV_REG_ENDPOINT   "a11homvea4zo8t-ats.iot.ap-east-1.amazonaws.com"

enum DEV_REG_STA {
    DEV_REG_STOP,
    DEV_UPGRADE_CONFIRM,  /*  Confirm upgrade   */
    DEV_UPGRADE_ENTRY,
    DEV_UPGRADE_STARTED,
    DEV_UPGRADE_DOWNLOADING,
    DEV_UPGRADE_COMPLETE,
    DEV_REG_START,
    DEV_REG_STATUS,
    DEV_REG_PRESS_ENTER,
    DEV_REG_POLL_FOR_WALLET,
    /* DEV_REG_WAIT_FOR_BUTTON_DOWN, */
    DEV_REG_ADDR_CHECK,
    DEV_REG_SIGN_SEND,
    DEV_REG_POLL_STATE,
    DEV_REG_FAIL,
    DEV_REG_SUCCESS,
};

bool IsDevReg(void);
void devRegSet(enum DEV_REG_STA esta);
/* void  iotexDevBinding(struct pollfd *fds, struct mqtt_client *client); */
int iotex_mqtt_get_wallet(const uint8_t *payload, uint32_t len);

#endif /*  #ifndef __DEV_REG_H__ */