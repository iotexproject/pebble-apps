/* Automatically generated nanopb header */
/* Generated by nanopb-0.4.5 */

#ifndef PB_PACKAGE_PB_H_INCLUDED
#define PB_PACKAGE_PB_H_INCLUDED
#include <pb.h>

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Enum definitions */
typedef enum _BinPackage_PackageType { 
    BinPackage_PackageType_DATA = 0, 
    BinPackage_PackageType_CONFIG = 1, 
    BinPackage_PackageType_STATE = 2 
} BinPackage_PackageType;

/* Struct definitions */
typedef PB_BYTES_ARRAY_T(500) BinPackage_data_t;
typedef struct _BinPackage { 
    BinPackage_PackageType type; 
    BinPackage_data_t data; 
    uint32_t timestamp; 
    pb_byte_t signature[64]; 
} BinPackage;

typedef PB_BYTES_ARRAY_T(200) ConfirmPackage_owner_t;
typedef struct _ConfirmPackage { 
    ConfirmPackage_owner_t owner; 
    uint32_t timestamp; 
    pb_byte_t signature[64]; 
    uint32_t channel; 
} ConfirmPackage;

typedef struct _SensorConfig { 
    bool has_bulkUpload;
    uint32_t bulkUpload; 
    bool has_dataChannel;
    uint32_t dataChannel; 
    bool has_uploadPeriod;
    uint32_t uploadPeriod; 
    bool has_bulkUploadSamplingCnt;
    uint32_t bulkUploadSamplingCnt; 
    bool has_bulkUploadSamplingFreq;
    uint32_t bulkUploadSamplingFreq; 
    bool has_beep;
    uint32_t beep; 
    bool has_firmware;
    char firmware[160]; 
    bool has_deviceConfigurable;
    bool deviceConfigurable; 
} SensorConfig;

typedef struct _SensorConfirm { 
    bool has_owner;
    char owner[200]; 
} SensorConfirm;

typedef struct _SensorData { 
    bool has_snr;
    uint32_t snr; 
    bool has_vbat;
    uint32_t vbat; 
    bool has_latitude;
    int32_t latitude; 
    bool has_longitude;
    int32_t longitude; 
    bool has_gasResistance;
    uint32_t gasResistance; 
    bool has_temperature;
    uint32_t temperature; 
    bool has_pressure;
    uint32_t pressure; 
    bool has_humidity;
    uint32_t humidity; 
    bool has_light;
    uint32_t light; 
    bool has_temperature2;
    uint32_t temperature2; 
    int32_t gyroscope[3]; 
    int32_t accelerometer[3]; 
    bool has_random;
    char random[17]; 
} SensorData;

typedef struct _SensorState { 
    bool has_state;
    uint32_t state; 
} SensorState;


/* Helper constants for enums */
#define _BinPackage_PackageType_MIN BinPackage_PackageType_DATA
#define _BinPackage_PackageType_MAX BinPackage_PackageType_STATE
#define _BinPackage_PackageType_ARRAYSIZE ((BinPackage_PackageType)(BinPackage_PackageType_STATE+1))


#ifdef __cplusplus
extern "C" {
#endif

/* Initializer values for message structs */
#define SensorData_init_default                  {false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, {0, 0, 0}, {0, 0, 0}, false, ""}
#define SensorConfig_init_default                {false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, "", false, 0}
#define SensorState_init_default                 {false, 0}
#define SensorConfirm_init_default               {false, ""}
#define BinPackage_init_default                  {_BinPackage_PackageType_MIN, {0, {0}}, 0, {0}}
#define ConfirmPackage_init_default              {{0, {0}}, 0, {0}, 0}
#define SensorData_init_zero                     {false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, {0, 0, 0}, {0, 0, 0}, false, ""}
#define SensorConfig_init_zero                   {false, 0, false, 0, false, 0, false, 0, false, 0, false, 0, false, "", false, 0}
#define SensorState_init_zero                    {false, 0}
#define SensorConfirm_init_zero                  {false, ""}
#define BinPackage_init_zero                     {_BinPackage_PackageType_MIN, {0, {0}}, 0, {0}}
#define ConfirmPackage_init_zero                 {{0, {0}}, 0, {0}, 0}

/* Field tags (for use in manual encoding/decoding) */
#define BinPackage_type_tag                      1
#define BinPackage_data_tag                      2
#define BinPackage_timestamp_tag                 3
#define BinPackage_signature_tag                 4
#define ConfirmPackage_owner_tag                 1
#define ConfirmPackage_timestamp_tag             2
#define ConfirmPackage_signature_tag             3
#define ConfirmPackage_channel_tag               4
#define SensorConfig_bulkUpload_tag              1
#define SensorConfig_dataChannel_tag             2
#define SensorConfig_uploadPeriod_tag            3
#define SensorConfig_bulkUploadSamplingCnt_tag   4
#define SensorConfig_bulkUploadSamplingFreq_tag  5
#define SensorConfig_beep_tag                    6
#define SensorConfig_firmware_tag                7
#define SensorConfig_deviceConfigurable_tag      8
#define SensorConfirm_owner_tag                  1
#define SensorData_snr_tag                       1
#define SensorData_vbat_tag                      2
#define SensorData_latitude_tag                  3
#define SensorData_longitude_tag                 4
#define SensorData_gasResistance_tag             5
#define SensorData_temperature_tag               6
#define SensorData_pressure_tag                  7
#define SensorData_humidity_tag                  8
#define SensorData_light_tag                     9
#define SensorData_temperature2_tag              10
#define SensorData_gyroscope_tag                 11
#define SensorData_accelerometer_tag             12
#define SensorData_random_tag                    13
#define SensorState_state_tag                    1

/* Struct field encoding specification for nanopb */
#define SensorData_FIELDLIST(X, a) \
X(a, STATIC,   OPTIONAL, UINT32,   snr,               1) \
X(a, STATIC,   OPTIONAL, UINT32,   vbat,              2) \
X(a, STATIC,   OPTIONAL, SINT32,   latitude,          3) \
X(a, STATIC,   OPTIONAL, SINT32,   longitude,         4) \
X(a, STATIC,   OPTIONAL, UINT32,   gasResistance,     5) \
X(a, STATIC,   OPTIONAL, UINT32,   temperature,       6) \
X(a, STATIC,   OPTIONAL, UINT32,   pressure,          7) \
X(a, STATIC,   OPTIONAL, UINT32,   humidity,          8) \
X(a, STATIC,   OPTIONAL, UINT32,   light,             9) \
X(a, STATIC,   OPTIONAL, UINT32,   temperature2,     10) \
X(a, STATIC,   FIXARRAY, SINT32,   gyroscope,        11) \
X(a, STATIC,   FIXARRAY, SINT32,   accelerometer,    12) \
X(a, STATIC,   OPTIONAL, STRING,   random,           13)
#define SensorData_CALLBACK NULL
#define SensorData_DEFAULT NULL

#define SensorConfig_FIELDLIST(X, a) \
X(a, STATIC,   OPTIONAL, UINT32,   bulkUpload,        1) \
X(a, STATIC,   OPTIONAL, UINT32,   dataChannel,       2) \
X(a, STATIC,   OPTIONAL, UINT32,   uploadPeriod,      3) \
X(a, STATIC,   OPTIONAL, UINT32,   bulkUploadSamplingCnt,   4) \
X(a, STATIC,   OPTIONAL, UINT32,   bulkUploadSamplingFreq,   5) \
X(a, STATIC,   OPTIONAL, UINT32,   beep,              6) \
X(a, STATIC,   OPTIONAL, STRING,   firmware,          7) \
X(a, STATIC,   OPTIONAL, BOOL,     deviceConfigurable,   8)
#define SensorConfig_CALLBACK NULL
#define SensorConfig_DEFAULT NULL

#define SensorState_FIELDLIST(X, a) \
X(a, STATIC,   OPTIONAL, UINT32,   state,             1)
#define SensorState_CALLBACK NULL
#define SensorState_DEFAULT NULL

#define SensorConfirm_FIELDLIST(X, a) \
X(a, STATIC,   OPTIONAL, STRING,   owner,             1)
#define SensorConfirm_CALLBACK NULL
#define SensorConfirm_DEFAULT NULL

#define BinPackage_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, UENUM,    type,              1) \
X(a, STATIC,   REQUIRED, BYTES,    data,              2) \
X(a, STATIC,   REQUIRED, UINT32,   timestamp,         3) \
X(a, STATIC,   REQUIRED, FIXED_LENGTH_BYTES, signature,         4)
#define BinPackage_CALLBACK NULL
#define BinPackage_DEFAULT NULL

#define ConfirmPackage_FIELDLIST(X, a) \
X(a, STATIC,   REQUIRED, BYTES,    owner,             1) \
X(a, STATIC,   REQUIRED, UINT32,   timestamp,         2) \
X(a, STATIC,   REQUIRED, FIXED_LENGTH_BYTES, signature,         3) \
X(a, STATIC,   REQUIRED, UINT32,   channel,           4)
#define ConfirmPackage_CALLBACK NULL
#define ConfirmPackage_DEFAULT NULL

extern const pb_msgdesc_t SensorData_msg;
extern const pb_msgdesc_t SensorConfig_msg;
extern const pb_msgdesc_t SensorState_msg;
extern const pb_msgdesc_t SensorConfirm_msg;
extern const pb_msgdesc_t BinPackage_msg;
extern const pb_msgdesc_t ConfirmPackage_msg;

/* Defines for backwards compatibility with code written before nanopb-0.4.0 */
#define SensorData_fields &SensorData_msg
#define SensorConfig_fields &SensorConfig_msg
#define SensorState_fields &SensorState_msg
#define SensorConfirm_fields &SensorConfirm_msg
#define BinPackage_fields &BinPackage_msg
#define ConfirmPackage_fields &ConfirmPackage_msg

/* Maximum encoded size of messages (where known) */
#define BinPackage_size                          577
#define ConfirmPackage_size                      281
#define SensorConfig_size                        200
#define SensorConfirm_size                       202
#define SensorData_size                          114
#define SensorState_size                         6

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
