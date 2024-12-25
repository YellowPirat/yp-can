#ifndef YP_CAN_REGS_H
#define YP_CAN_REGS_H

#include <linux/types.h>

// Register offsets
#define REG_STATUS_BUFFER    0x00
#define REG_STATUS_ERROR     0x04
#define REG_STATUS_MISSED    0x08
#define REG_FRAME_TYPE       0x0c
#define REG_TIMESTAMP_LOW    0x10
#define REG_TIMESTAMP_HIGH   0x14
#define REG_CAN_ID           0x18
#define REG_DLC              0x1c
#define REG_CRC              0x20
#define REG_DATA_LOW         0x24
#define REG_DATA_HIGH        0x28

// Register structures
union buffer_status_reg {
    u32 raw;
    struct {
        u32 buffer_usage:10;     // FIFO buffer usage
        u32 reserved:22;        // Reserved, reads as 0
    };
};

union error_status_reg {
    u32 raw;
    struct {
        u32 peripheral_error:16; // Various error states
        u32 reserved:16;        // Reserved, reads as 0
    };
};

union missed_status_reg {
    u32 raw;
    struct {
        u32 missed_frames:24;   // Counter of lost frames
        u32 overflow:1;         // Counter overflow flag
        u32 reserved:7;        // Reserved, reads as 0
    };
};

union frame_type_reg {
    u32 raw;
    struct {
        u32 stuff_error:1;      // Bit stuffing error
        u32 form_error:1;       // Form error
        u32 sample_error:1;     // Sample error
        u32 crc_error:1;        // CRC error
        u32 reserved_errors:12;  // Reserved for future error types
        u32 frame_type:8;       // CAN frame type (2.0, FD, XL etc)
        u32 reserved:8;         // Reserved, reads as 0
    };
};


union can_id_reg {
    u32 raw;
    struct {
        u32 id:29;             // CAN ID (11-bit or 29-bit)
        u32 rtr:1;             // Remote transmission request
        u32 eff:1;             // Extended frame format
        u32 err:1;             // Error frame flag
    };
};

union dlc_reg {
    u32 raw;
    struct {
        u32 dlc:4;             // Data length code
        u32 reserved:28;       // Reserved, reads as 0
    };
};

union crc_reg {
    u32 raw;
    struct {
        u32 crc:15;            // CRC value
        u32 reserved:17;       // Reserved, reads as 0
    };
};

// Full hardware registers structure
struct yp_can_regs {
    union buffer_status_reg buffer_status;
    union error_status_reg error_status;
    union missed_status_reg missed_status;
    union frame_type_reg frame_type;
    u64 timestamp;
    union can_id_reg can_id;
    union dlc_reg dlc;
    union crc_reg crc;
    u64 data;
};

#endif /* YP_CAN_REGS_H */
