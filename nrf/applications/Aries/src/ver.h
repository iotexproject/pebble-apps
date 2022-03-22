
#ifndef __APP_VERSION__
#define __APP_VERSION__

/*  hardware board version */
#define HW_VERSION "3.0"

/*  nordic sdk version */
#define SDK_VERSION "1.4.0"

#define  IOTEX_APP_NAME  "Aries"

#define  RELEASE_VERSION    "1.0.1"

/*  application version */
#define IOTEX_APP_VERSION IOTEX_APP_NAME" "RELEASE_VERSION

#define APP_VERSION_INFO HW_VERSION"_"SDK_VERSION"_"IOTEX_APP_VERSION">"

/*  upload period, second */
#define SENSOR_UPLOAD_PERIOD       300  

/* which backend does device connected */
#define   BACKEND_ID        "1"

#endif /*  #ifndef __APP_VERSION__ */
