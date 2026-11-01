# S7 absolute DB map — read-only application contract

Do **not** identify this as a real Siemens S7-1200 firmware. All offset values are zero-based bytes. REAL32 and numeric multibyte values are big-endian. U8 Boolean is a full byte.

## DB1 (256 bytes)

| Key | Byte offset | S7 type | Unit |
|---|---:|---|---|
| `state_code` | 0 | UINT16 | enum |
| `reason_code` | 2 | UINT16 | enum |
| `controller_mode` | 4 | UINT16 | enum |
| `data_quality_code` | 6 | UINT16 | enum |
| `sequence` | 8 | UINT32 | count |
| `unix_utc_s` | 12 | UINT32 | s |
| `spindle_target_rpm` | 16 | REAL32 | rpm |
| `spindle_actual_rpm` | 20 | REAL32 | rpm |
| `spindle_load_pct` | 24 | REAL32 | % |
| `spindle_temp_c` | 28 | REAL32 | C |
| `feed_command_mm_min` | 32 | REAL32 | mm/min |
| `feed_actual_mm_min` | 36 | REAL32 | mm/min |
| `feed_override_pct` | 40 | REAL32 | % |
| `spindle_override_pct` | 44 | REAL32 | % |
| `axis_x_mm` | 48 | REAL32 | mm |
| `axis_y_mm` | 52 | REAL32 | mm |
| `axis_z_mm` | 56 | REAL32 | mm |
| `axis_x_velocity_mm_s` | 60 | REAL32 | mm/s |
| `axis_y_velocity_mm_s` | 64 | REAL32 | mm/s |
| `axis_z_velocity_mm_s` | 68 | REAL32 | mm/s |
| `vibration_rms_mm_s` | 72 | REAL32 | mm/s |
| `coolant_temp_c` | 76 | REAL32 | C |
| `coolant_flow_l_min` | 80 | REAL32 | L/min |
| `coolant_pressure_bar` | 84 | REAL32 | bar |
| `power_kw` | 88 | REAL32 | kW |
| `energy_kwh` | 92 | REAL32 | kWh |
| `hydraulic_pressure_bar` | 96 | REAL32 | bar |
| `air_pressure_bar` | 100 | REAL32 | bar |
| `ambient_temp_c` | 104 | REAL32 | C |
| `cabinet_temp_c` | 108 | REAL32 | C |
| `is_cutting` | 112 | U8 | boolean |
| `spindle_on` | 113 | U8 | boolean |
| `coolant_on` | 114 | U8 | boolean |
| `door_closed` | 115 | U8 | boolean |
| `estop_active` | 116 | U8 | boolean |
| `alarm_active` | 117 | U8 | boolean |
| `cycle_active` | 118 | U8 | boolean |
| `part_clamped` | 119 | U8 | boolean |
| `current_tool_id` | 120 | UINT16 | id |
| `tool_change_counter` | 124 | UINT32 | count |
| `program_block` | 128 | UINT32 | count |
| `cycle_elapsed_s` | 132 | REAL32 | s |
| `cycle_target_s` | 136 | REAL32 | s |
| `program_progress_pct` | 140 | REAL32 | % |
| `part_progress_pct` | 144 | REAL32 | % |
| `last_heartbeat_utc_s` | 148 | UINT32 | s |

## DB2 (256 bytes)

| Key | Byte offset | S7 type | Unit |
|---|---:|---|---|
| `active_tool_id` | 0 | UINT16 | id |
| `magazine_tool_count` | 2 | UINT16 | count |
| `active_wear_pct` | 4 | REAL32 | % |
| `active_life_remaining_min` | 8 | REAL32 | min |
| `active_cutting_minutes` | 12 | REAL32 | min |
| `active_diameter_wear_mm` | 16 | REAL32 | mm |
| `active_length_offset_mm` | 20 | REAL32 | mm |
| `active_load_factor` | 24 | REAL32 | ratio |
| `active_health_code` | 28 | UINT16 | enum |
| `replacement_due` | 30 | UINT16 | boolean |
| `toolchange_inprogress` | 32 | UINT16 | boolean |
| `active_tool_type` | 34 | UINT16 | enum |
| `tool_01_tool_id` | 64 | UINT16 | id |
| `tool_01_tool_type` | 66 | UINT16 | enum |
| `tool_01_wear_pct` | 68 | REAL32 | % |
| `tool_01_cutting_minutes` | 72 | REAL32 | min |
| `tool_01_life_remaining_minutes` | 76 | REAL32 | min |
| `tool_02_tool_id` | 80 | UINT16 | id |
| `tool_02_tool_type` | 82 | UINT16 | enum |
| `tool_02_wear_pct` | 84 | REAL32 | % |
| `tool_02_cutting_minutes` | 88 | REAL32 | min |
| `tool_02_life_remaining_minutes` | 92 | REAL32 | min |
| `tool_03_tool_id` | 96 | UINT16 | id |
| `tool_03_tool_type` | 98 | UINT16 | enum |
| `tool_03_wear_pct` | 100 | REAL32 | % |
| `tool_03_cutting_minutes` | 104 | REAL32 | min |
| `tool_03_life_remaining_minutes` | 108 | REAL32 | min |
| `tool_04_tool_id` | 112 | UINT16 | id |
| `tool_04_tool_type` | 114 | UINT16 | enum |
| `tool_04_wear_pct` | 116 | REAL32 | % |
| `tool_04_cutting_minutes` | 120 | REAL32 | min |
| `tool_04_life_remaining_minutes` | 124 | REAL32 | min |
| `tool_05_tool_id` | 128 | UINT16 | id |
| `tool_05_tool_type` | 130 | UINT16 | enum |
| `tool_05_wear_pct` | 132 | REAL32 | % |
| `tool_05_cutting_minutes` | 136 | REAL32 | min |
| `tool_05_life_remaining_minutes` | 140 | REAL32 | min |
| `tool_06_tool_id` | 144 | UINT16 | id |
| `tool_06_tool_type` | 146 | UINT16 | enum |
| `tool_06_wear_pct` | 148 | REAL32 | % |
| `tool_06_cutting_minutes` | 152 | REAL32 | min |
| `tool_06_life_remaining_minutes` | 156 | REAL32 | min |
| `tool_07_tool_id` | 160 | UINT16 | id |
| `tool_07_tool_type` | 162 | UINT16 | enum |
| `tool_07_wear_pct` | 164 | REAL32 | % |
| `tool_07_cutting_minutes` | 168 | REAL32 | min |
| `tool_07_life_remaining_minutes` | 172 | REAL32 | min |
| `tool_08_tool_id` | 176 | UINT16 | id |
| `tool_08_tool_type` | 178 | UINT16 | enum |
| `tool_08_wear_pct` | 180 | REAL32 | % |
| `tool_08_cutting_minutes` | 184 | REAL32 | min |
| `tool_08_life_remaining_minutes` | 188 | REAL32 | min |
| `tool_09_tool_id` | 192 | UINT16 | id |
| `tool_09_tool_type` | 194 | UINT16 | enum |
| `tool_09_wear_pct` | 196 | REAL32 | % |
| `tool_09_cutting_minutes` | 200 | REAL32 | min |
| `tool_09_life_remaining_minutes` | 204 | REAL32 | min |
| `tool_10_tool_id` | 208 | UINT16 | id |
| `tool_10_tool_type` | 210 | UINT16 | enum |
| `tool_10_wear_pct` | 212 | REAL32 | % |
| `tool_10_cutting_minutes` | 216 | REAL32 | min |
| `tool_10_life_remaining_minutes` | 220 | REAL32 | min |
| `tool_11_tool_id` | 224 | UINT16 | id |
| `tool_11_tool_type` | 226 | UINT16 | enum |
| `tool_11_wear_pct` | 228 | REAL32 | % |
| `tool_11_cutting_minutes` | 232 | REAL32 | min |
| `tool_11_life_remaining_minutes` | 236 | REAL32 | min |
| `tool_12_tool_id` | 240 | UINT16 | id |
| `tool_12_tool_type` | 242 | UINT16 | enum |
| `tool_12_wear_pct` | 244 | REAL32 | % |
| `tool_12_cutting_minutes` | 248 | REAL32 | min |
| `tool_12_life_remaining_minutes` | 252 | REAL32 | min |

## DB3 (256 bytes)

| Key | Byte offset | S7 type | Unit |
|---|---:|---|---|
| `job_id` | 0 | UINT32 | id |
| `program_id` | 4 | UINT32 | id |
| `part_id` | 8 | UINT32 | id |
| `job_target_qty` | 12 | UINT32 | count |
| `job_completed_qty` | 16 | UINT32 | count |
| `job_good_qty` | 20 | UINT32 | count |
| `job_reject_qty` | 24 | UINT32 | count |
| `current_cycle_elapsed_s` | 28 | REAL32 | s |
| `nominal_cycle_s` | 32 | REAL32 | s |
| `job_progress_pct` | 36 | REAL32 | % |
| `current_block` | 40 | UINT16 | count |
| `current_step_type` | 42 | UINT16 | enum |
| `shift_id` | 44 | UINT16 | enum |
| `job_state_code` | 46 | UINT16 | enum |
| `total_parts_count` | 48 | UINT32 | count |
| `total_good_count` | 52 | UINT32 | count |
| `total_reject_count` | 56 | UINT32 | count |
| `cycle_sequence` | 60 | UINT32 | count |
| `last_cycle_duration_s` | 64 | REAL32 | s |
| `estimated_job_remaining_s` | 68 | REAL32 | s |
| `last_reject_code` | 72 | UINT16 | enum |
| `pending_job_count` | 74 | UINT16 | count |
| `order_label` | 80 | STRING32 | ascii |
| `program_label` | 114 | STRING32 | ascii |
| `part_label` | 148 | STRING32 | ascii |

## DB4 (128 bytes)

| Key | Byte offset | S7 type | Unit |
|---|---:|---|---|
| `alarm_code` | 0 | UINT16 | enum |
| `alarm_severity` | 2 | UINT16 | enum |
| `alarm_latched` | 4 | U8 | boolean |
| `alarm_acknowledged` | 5 | U8 | boolean |
| `alarm_requires_operator` | 6 | U8 | boolean |
| `maintenance_due` | 7 | U8 | boolean |
| `alarm_started_unix_s` | 8 | UINT32 | s |
| `alarm_duration_s` | 12 | UINT32 | s |
| `last_alarm_code` | 16 | UINT16 | enum |
| `last_stop_reason` | 18 | UINT16 | enum |
| `event_sequence` | 20 | UINT32 | count |
| `last_event_utc_s` | 24 | UINT32 | s |
| `availability_state` | 28 | UINT16 | enum |
| `quality_state` | 30 | UINT16 | enum |
| `alarm_text` | 32 | STRING48 | ascii |

### Tool magazine
DB2 bytes 64–255 represent 12 slots of 16 bytes. The CSV is the canonical source for these offsets.

### OPC UA and MQTT mapping
Each CSV row has `opc_node_id`, `opc_group` and `mqtt_path`. Verify the discovered OPC UA namespace index at runtime against the declared `ns=2` contract. Never claim full S7-1200 firmware compatibility.
