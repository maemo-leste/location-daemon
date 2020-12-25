/*
 * Copyright (c) 2020 Ivan J. <parazyd@dyne.org>
 *
 * This file is part of location-daemon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <dbus/dbus.h>
#include <gps.h>

#include "arg.h"

/* macros */
#define TSTONS(ts) ((double)((ts).tv_sec + ((ts).tv_nsec / 1e9)))
#define nelem(x) (sizeof (x) / sizeof *(x))

/* enums */
#define GPSD_HOST "localhost"
#define GPSD_PORT "2947"

#define DBUS_OBJECT_ROOT     "/org/maemo/LocationDaemon"
#define DBUS_SERVICE         "org.maemo.LocationDaemon"
#define DEVICE_INTERFACE     DBUS_SERVICE".Device"
#define ACCURACY_INTERFACE   DBUS_SERVICE".Accuracy"
#define COURSE_INTERFACE     DBUS_SERVICE".Course"
#define POSITION_INTERFACE   DBUS_SERVICE".Position"
#define SATELLITE_INTERFACE  DBUS_SERVICE".Satellite"
#define TIME_INTERFACE       DBUS_SERVICE".Time"

/* function declarations */
static void usage(void);
static void dbus_send_va(const char *, const char *, int, ...);
static void poll_and_publish_gpsd_data(void);

/* variables */
char *argv0;
DBusConnection *dbus;
struct gps_data_t gpsdata;
int mode = MODE_NOT_SEEN;
double dtime = NAN;
double ept = NAN;
double lat = NAN;
double lon = NAN;
double eph = NAN;
double alt = NAN;
double epv = NAN;
double trk = NAN;
double epd = NAN;
double spd = NAN;
double eps = NAN;
double clb = NAN;
double epc = NAN;

void usage(void)
{
	printf("Usage: %s [-t N]\n", argv0);
	printf("\t-t N:\tgpsd polling interval in seconds\n");
	exit(1);
}

void dbus_send_va(const char *interface, const char *sig, int f, ...)
{
	DBusMessage *msg;
	dbus_uint32_t serial = 0;

	msg = dbus_message_new_signal(DBUS_OBJECT_ROOT, interface, sig);
	if (msg == NULL) {
		fprintf(stderr, "dbus_send_double: %s message NULL\n", sig);
		return;
	}

	va_list var_args;
	va_start(var_args, f);

	if (!dbus_message_append_args_valist(msg, f, var_args)) {
		fprintf(stderr, "dbus_send_va: %s: out of memory on append\n", sig);
		return;
	}

	va_end(var_args);

	if (!dbus_connection_send(dbus, msg, &serial)) {
		fprintf(stderr, "dbus_send_va: %s: out of memory on send\n", sig);
		return;
	}

	dbus_connection_flush(dbus);
	dbus_message_unref(msg);
}

void poll_and_publish_gpsd_data(void)
{
	if (gps_waiting(&gpsdata, 500)) {
		errno = 0;
		if (gps_read(&gpsdata, NULL, 0) == -1) {
			fprintf(stderr, "gpsd read error: %d, %s\n", errno,
				gps_errstr(errno));
			return;
		}

		struct gps_fix_t f = gpsdata.fix;

		if (mode != f.mode) {
			mode = f.mode;
			dbus_send_va(DEVICE_INTERFACE, "FixStatusChanged",
				DBUS_TYPE_INT32, &mode,
				DBUS_TYPE_INVALID);
		}

		if (gpsdata.set & TIME_SET) {
			if (dtime != TSTONS(f.time)) {
				dtime = TSTONS(f.time);
				dbus_send_va(TIME_INTERFACE, "TimeChanged",
				DBUS_TYPE_DOUBLE, &dtime,
				DBUS_TYPE_INVALID);
			}
		}

		if ((isfinite(f.latitude) && isfinite(f.longitude))
			|| isfinite(f.altHAE)) {
			if (lat != f.latitude || lon != f.longitude || alt != f.altHAE) {
				lat = f.latitude;
				lon = f.longitude;
				alt = f.altHAE;
				dbus_send_va(POSITION_INTERFACE, "PositionChanged",
					DBUS_TYPE_DOUBLE, &lat,
					DBUS_TYPE_DOUBLE, &lon,
					DBUS_TYPE_DOUBLE, &alt,
					DBUS_TYPE_INVALID);
			}
		}

		if (isfinite(f.speed) || isfinite(f.track) || isfinite(f.climb)) {
			if (spd != f.speed || trk != f.track || clb != f.climb) {
				spd = f.speed;
				trk = f.track;
				clb = f.climb;
				dbus_send_va(COURSE_INTERFACE, "CourseChanged",
					DBUS_TYPE_DOUBLE, &spd,
					DBUS_TYPE_DOUBLE, &trk,
					DBUS_TYPE_DOUBLE, &clb,
					DBUS_TYPE_INVALID);
			}
		}

		if (isfinite(f.eph) || isfinite(f.epv) || isfinite(f.eps)
			|| isfinite(f.ept) || isfinite(f.epc)) {
			if (eph != f.eph || epv != f.epv || eps != f.eps
				|| ept != f.ept || epc != f.epc) {
				eph = f.eph;
				epv = f.epv;
				eps = f.eps;
				ept = f.ept;
				epc = f.epc;
				dbus_send_va(ACCURACY_INTERFACE, "AccuracyChanged",
					DBUS_TYPE_DOUBLE, &eph,
					DBUS_TYPE_DOUBLE, &epv,
					DBUS_TYPE_DOUBLE, &eps,
					DBUS_TYPE_DOUBLE, &ept,
					DBUS_TYPE_DOUBLE, &epc,
					DBUS_TYPE_INVALID);
			}
		}
	}
}

int main(int argc, char *argv[])
{
	int interval = 1; /* gpsd polling interval in seconds */
	int ret;
	DBusError err;

	ARGBEGIN {
	case 't':
		interval = atoi(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;

	dbus_error_init(&err);

	dbus = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (dbus_error_is_set(&err)) {
		fprintf(stderr, "DBus connection error (%s)\n", err.message);
		return 1;
	}

	if (dbus == NULL)
		return 1;

	ret = dbus_bus_request_name(dbus, DBUS_SERVICE,
		DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
	if (dbus_error_is_set(&err)) {
		fprintf(stderr, "Name error (%s)\n", err.message);
		dbus_error_free(&err);
	}
	if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) {
		return 1;
	}

	if (gps_open(GPSD_HOST, GPSD_PORT, &gpsdata)) {
		fprintf(stderr, "Could not open gpsd socket: %d, %s\n", errno,
			gps_errstr(errno));
		return 1;
	}

	(void) gps_stream(&gpsdata, WATCH_ENABLE, NULL);

	while (1) {
		/* TODO: loop until caught signal or something */
		poll_and_publish_gpsd_data();
		sleep(interval);
	}

	(void) gps_stream(&gpsdata, WATCH_DISABLE, NULL);
	(void) gps_close(&gpsdata);

	return 0;
}
