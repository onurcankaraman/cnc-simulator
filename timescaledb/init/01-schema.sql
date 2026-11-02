CREATE EXTENSION IF NOT EXISTS timescaledb;
REVOKE CREATE ON SCHEMA public FROM PUBLIC;
CREATE TABLE telemetry(ts_ms bigint PRIMARY KEY,seq bigint NOT NULL,state_code integer,job_id bigint,program_id integer,tool_id integer,spindle_rpm double precision,load_pct double precision,feed_mm_min double precision,x_mm double precision,y_mm double precision,z_mm double precision,vibration double precision,spindle_temp_c double precision,power_kw double precision,energy_kwh double precision,wear_pct double precision,coolant_flow double precision,alarm_code integer,good_count bigint,scrap_count bigint,payload_json jsonb NOT NULL);
SELECT create_hypertable('telemetry',by_range('ts_ms',3600000::bigint));
CREATE TABLE events(id bigint,ts_ms bigint NOT NULL,type text,code integer,severity integer,state_from integer,state_to integer,details_json jsonb,PRIMARY KEY(ts_ms,id));
SELECT create_hypertable('events',by_range('ts_ms',3600000::bigint));
CREATE TABLE orders(order_id bigint PRIMARY KEY,program_id integer,part_id integer,target_qty integer,good_qty integer,scrap_qty integer,started_ms bigint,completed_ms bigint,status text);
CREATE TABLE runtime_state(key text PRIMARY KEY,value_json jsonb NOT NULL,updated_ms bigint);
CREATE FUNCTION cnc_now_ms() RETURNS bigint LANGUAGE SQL STABLE AS $$ SELECT (extract(epoch FROM now())*1000)::bigint $$;
SELECT set_integer_now_func('telemetry','cnc_now_ms');
SELECT set_integer_now_func('events','cnc_now_ms');
-- Drop whole expired chunks, then delete the expired portion of the boundary chunk.
-- Runtime state and active orders are deliberately excluded.
CREATE FUNCTION cnc_cleanup(now_ms bigint) RETURNS void LANGUAGE plpgsql SECURITY DEFINER SET search_path=public,pg_temp AS $$
BEGIN
 PERFORM drop_chunks('public.telemetry',older_than => now_ms-86400000);
 PERFORM drop_chunks('public.events',older_than => now_ms-86400000);
 DELETE FROM public.telemetry WHERE ts_ms<now_ms-86400000;
 DELETE FROM public.events WHERE ts_ms<now_ms-86400000;
 DELETE FROM public.orders WHERE status!='active' AND completed_ms<now_ms-86400000;
END $$;
REVOKE ALL ON FUNCTION cnc_cleanup(bigint) FROM PUBLIC;
CREATE PROCEDURE cnc_retention_job(job_id int,config jsonb) LANGUAGE plpgsql AS $$
BEGIN PERFORM cnc_cleanup(cnc_now_ms()); END $$;
SELECT add_job('cnc_retention_job',INTERVAL '1 minute');
