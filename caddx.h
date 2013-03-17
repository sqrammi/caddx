#ifndef __CADDX_H__
#define __CADDX_H__

#include <stdint.h>
#include <stdbool.h>

#define CADDX_START		0x7e
#define CADDX_START_ESC		0x20
#define CADDX_ACK_REQ		0x80
#define CADDX_MSG_MASK		0x3f

#define CADDX_IFACE_CFG		0x01
#define CADDX_IFACE_CFG_REQ	0x21
#define CADDX_PART_STATUS_REQ	0x26
#define CADDX_ACK		0x1d
#define CADDX_NAK		0x1e

#define __packed __attribute__((packed))

struct caddx_msg {
	uint8_t type:6;
	bool reserved06:1;
	bool ack:1;
} __packed;

#define CADDX_ZONE_STATUS	0x04
struct caddx_zone_status {
	struct caddx_msg msg;

	uint8_t zone;

	bool part1_en:1;
	bool part2_en:1;
	bool part3_en:1;
	bool part4_en:1;
	bool part5_en:1;
	bool part6_en:1;
	bool part7_en:1;
	bool part8_en:1;

	bool fire:1;
	bool hour_24:1;
	bool key_switch:1;
	bool follower:1;
	bool entry_exit_delay_1:1;
	bool entry_exit_delay_2:1;
	bool interior:1;
	bool local_only:1;

	bool keypad_sounder:1;
	bool yelping_siren:1;
	bool steady_siren:1;
	bool chime:1;
	bool bypassable:1;
	bool group_bypassable:1;
	bool force_armable:1;
	bool entry_guard:1;

	bool fast_loop_response:1;
	bool double_eol_tamper:1;
	bool trouble0:1;
	bool cross_zone:1;
	bool dialer_delay:1;
	bool swinger_shutdown:1;
	bool restorable:1;
	bool listen_in:1;

	bool faulted:1;
	bool tampered:1;
	bool trouble:1;
	bool bypassed:1;
	bool inhibited:1;
	bool low_battery:1;
	bool loss_of_supervision:1;
	bool reserved77:1;

	bool alarm_memory:1;
	bool bypass_memory:1;
	bool reserved82:1;
	bool reserved83:1;
	bool reserved84:1;
	bool reserved85:1;
	bool reserved86:1;
	bool reserved87:1;
} __packed;

#define CADDX_PART_STATUS_REQ	0x26
struct caddx_part_status_req {
	struct caddx_msg msg;
	uint8_t part;
};

#define CADDX_ZONE_STATUS_REQ	0x24
struct caddx_zone_status_req {
	struct caddx_msg msg;
	uint8_t zone;
};

#define CADDX_PART_STATUS	0x06
struct caddx_part_status {
	struct caddx_msg msg;

	uint8_t part;

	bool bypass_code_required:1;
	bool fire_trouble:1;
	bool fire:1;
	bool pulsing_buzzer:1;
	bool tlm_fault_memory:1;
	bool reserved25:1;
	bool armed:1;
	bool instant:1;

	bool previous_alarm:1;
	bool siren_on:1;
	bool steady_siren_on:1;
	bool alarm_memory:1;
	bool tamper:1;
	bool cancel_command_entered:1;
	bool code_entered:1;
	bool cancel_pending:1;

	bool reserved40:1;
	bool silent_exit_enabled:1;
	bool entryguard:1;
	bool chime_mode_on:1;
	bool entry:1;
	bool delay_expiration_warning:1;
	bool exit1:1;
	bool exit2:1;

	bool led_extinguish:1;
	bool cross_timing:1;
	bool recent_closing_being_timed:1;
	bool reserved53:1;
	bool exit_error_triggered:1;
	bool auto_home_inhibited:1;
	bool sensor_low_battery:1;
	bool sensor_lost_supervision:1;

	uint8_t last_user_number;

	bool zone_bypassed:1;
	bool force_arm_triggered_by_auto_arm:1;
	bool ready_to_arm:1;
	bool ready_to_force_arm:1;
	bool valid_pin_accepted:1;
	bool chime_on:1;
	bool error_beep:1;
	bool tone_on:1;

	bool entry1:1;
	bool open_period:1;
	bool alarm_sent_using_phone_1:1;
	bool alarm_sent_using_phone_2:1;
	bool alarm_sent_using_phone_3:1;
	bool cancel_report_is_in_stack:1;
	bool keyswitch_armed:1;
	bool delay_trip_in_progress:1;
} __packed;

#define CADDX_KEYPAD_FUNC0	0x3c
struct caddx_keypad_func0 {
	struct caddx_msg msg;

	uint8_t pin1:4;
	uint8_t pin2:4;

	uint8_t pin3:4;
	uint8_t pin4:4;

	uint8_t pin5:4;
	uint8_t pin6:4;

	uint8_t function;
#define CADDX_TURN_OFF_SOUNDER	0x00
#define CADDX_DISARM		0x01
#define CADDX_ARM_AWAY		0x02
#define CADDX_ARM_STAY		0x03
#define CADDX_CANCEL		0x04
#define CADDX_AUTO_ARM		0x05
#define CADDX_START_WALK_TEST	0x06
#define CADDX_STOP_WALK_TEST	0x07

	union {
		struct {
			uint8_t part1:1;
			uint8_t part2:1;
			uint8_t part3:1;
			uint8_t part4:1;
			uint8_t part5:1;
			uint8_t part6:1;
			uint8_t part7:1;
			uint8_t part8:1;
		};
		uint8_t part;
	};
};

#define CADDX_KEYPAD_FUNC0_NOPIN	0x3d
struct caddx_keypad_func0_nopin {
	struct caddx_msg msg;

	uint8_t function;

	union {
		struct {
			uint8_t part1:1;
			uint8_t part2:1;
			uint8_t part3:1;
			uint8_t part4:1;
			uint8_t part5:1;
			uint8_t part6:1;
			uint8_t part7:1;
			uint8_t part8:1;
		};
		uint8_t part;
	};
};

#define CADDX_KEYPAD_FUNC1	0x3e
struct caddx_keypad_func1 {
	struct caddx_msg msg;

#define CADDX_STAY		0x00
#define CADDX_CHIME		0x01
#define CADDX_EXIT		0x02
#define CADDX_BYPASS_INTERIORS	0x03
#define CADDX_FIRE_PANIC	0x04
#define CADDX_MEDICAL_PANIC	0x05
#define CADDX_POLICE_PANIC	0x06
#define CADDX_SMOKE_RESET	0x07
#define CADDX_AUTO_CALLBACK	0x08
#define CADDX_MANUAL_PICKUP	0x09
#define CADDX_SILENT_EXIT_ON	0x0a
#define CADDX_PERFORM_TEST	0x0b
#define CADDX_GROUP_BYPASS	0x0c
#define CADDX_AUX1		0x0d
#define CADDX_AUX2		0x0e
#define CADDX_KEYPAD_SOUNDER	0x0f
	uint8_t function;

	union {
		struct {
			uint8_t part1:1;
			uint8_t part2:1;
			uint8_t part3:1;
			uint8_t part4:1;
			uint8_t part5:1;
			uint8_t part6:1;
			uint8_t part7:1;
			uint8_t part8:1;
		};
		uint8_t part;
	};
};

#define CADDX_BYPASS_TOGGLE	0x3f
struct caddx_bypass_toggle {
	struct caddx_msg msg;
	uint8_t zone;
};

#endif /* __CADDX_H__ */
