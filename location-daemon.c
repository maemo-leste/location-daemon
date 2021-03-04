/*
 * Copyright (c) 2020-2021 Ivan J. <parazyd@dyne.org>
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
#include <float.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include <dbus/dbus.h>
#include <glib.h>
#include <glib-unix.h>
#include <gps.h>

/* enums */
#define GPSD_HOST "localhost"
#define GPSD_PORT "2947"

#define FLOCK_PATH "/run/lock/location-daemon.lock"

#define DBUS_OBJECT_ROOT     "/org/maemo/LocationDaemon"
#define DBUS_SERVICE         "org.maemo.LocationDaemon"
#define ACCURACY_INTERFACE   DBUS_SERVICE".Accuracy"
#define COURSE_INTERFACE     DBUS_SERVICE".Course"
#define DEVICE_INTERFACE     DBUS_SERVICE".Device"
#define POSITION_INTERFACE   DBUS_SERVICE".Position"
#define SATELLITE_INTERFACE  DBUS_SERVICE".Satellite"
#define TIME_INTERFACE       DBUS_SERVICE".Time"

/* function declarations */
static int sighandler(gpointer);
static int isequal(double, double);
static void dbus_send_va(const char *, const char *, int, ...);
static int poll_and_publish_gpsd_data(gpointer);
static int acquire_flock(gpointer);

/* variables */
static GMainLoop *mainloop;
static DBusConnection *dbus;
static struct gps_data_t gpsdata;
/* static struct satellite_t skyview[MAXCHANNELS]; */
static int mode = MODE_NOT_SEEN;
static double ept = 0.0 / 0.0;
static double lat = 0.0 / 0.0;
static double lon = 0.0 / 0.0;
static double eph = 0.0 / 0.0;
static double alt = 0.0 / 0.0;
static double epv = 0.0 / 0.0;
static double trk = 0.0 / 0.0;
static double epd = 0.0 / 0.0;
static double spd = 0.0 / 0.0;
static double eps = 0.0 / 0.0;
static double clb = 0.0 / 0.0;
static double epc = 0.0 / 0.0;

int sighandler(gpointer sig)
{
	int s = GPOINTER_TO_INT(sig);

	switch (s) {
	case SIGINT:
	case SIGHUP:
	case SIGTERM:
		g_debug("Caught %s", strsignal(s));
		if (mainloop != NULL)
			g_main_loop_quit(mainloop);
		break;
	}

	return 0;
}

int isequal(double a, double b)
{
	double diff = fabs(a - b);
	a = fabs(a);
	b = fabs(b);
	double largest = (b > a) ? b : a;
	if (diff <= largest * FLT_EPSILON)
		return 1;
	return 0;
}

void dbus_send_va(const char *interface, const char *sig, int f, ...)
{
	DBusMessage *msg;
	dbus_uint32_t serial = 0;

	msg = dbus_message_new_signal(DBUS_OBJECT_ROOT, interface, sig);
	if (msg == NULL) {
		g_warning("dbus_send_double: %s message NULL", sig);
		return;
	}

	va_list var_args;
	va_start(var_args, f);

	if (!dbus_message_append_args_valist(msg, f, var_args)) {
		g_warning("dbus_send_va: %s: out of memory on append", sig);
		va_end(var_args);
		goto out;
	}

	va_end(var_args);

	if (!dbus_connection_send(dbus, msg, &serial)) {
		g_warning("dbus_send_va: %s: out of memory on send", sig);
		goto out;
	}

	dbus_connection_flush(dbus);
 out:
	dbus_message_unref(msg);
}

int poll_and_publish_gpsd_data(gpointer data)
{
	g_message(G_STRFUNC);
	if (gps_waiting(&gpsdata, 500)) {
		errno = 0;
		if (gps_read(&gpsdata, NULL, 0) == -1) {
			g_warning("gpsd read error: %d, %s", errno, gps_errstr(errno));
			return TRUE;
		}
	} else {
		return TRUE;
	}

	struct gps_fix_t *f = &gpsdata.fix;

	if (mode != f->mode) {
		mode = f->mode;
		dbus_send_va(DEVICE_INTERFACE, "FixStatusChanged",
			     DBUS_TYPE_BYTE, &mode, DBUS_TYPE_INVALID);
	}

	/*
	if (gpsdata.satellites_visible > 0) {
		int c = 0;
		for (int i = 0; i < gpsdata.satellites_visible; i++) {
			if (!isequal(gpsdata.skyview[i].ss, skyview[i].ss)
			    || gpsdata.skyview[i].used != skyview[i].used
			    || gpsdata.skyview[i].PRN != skyview[i].PRN
			    || !isequal(gpsdata.skyview[i].elevation,
					skyview[i].elevation)
			    || !isequal(gpsdata.skyview[i].azimuth,
					skyview[i].azimuth)) {
				c = 1;
				memcpy(&gpsdata.skyview[i], &skyview[i],
				       sizeof(struct satellite_t));
			}
		}

		TODO: Decide how to publish satellites on dbus
		if (c) {
			fprintf(stderr, "sats changed\n");
		}
	}
	*/

	/* Time updates on every iteration, so there is no need for checks */
	if (gpsdata.set & TIME_SET) {
		dbus_send_va(TIME_INTERFACE, "TimeChanged",
			     DBUS_TYPE_INT64, &f->time.tv_sec,
			     DBUS_TYPE_INT64, &f->time.tv_nsec,
			     DBUS_TYPE_INVALID);
	}

	if (isfinite(f->latitude) || isfinite(f->longitude)
	    || isfinite(f->altMSL)) {
		if (!isequal(lat, f->latitude) || !isequal(lon, f->longitude)
		    || !isequal(alt, f->altMSL)
		    || !isfinite(lat) || !isfinite(lon) || !isfinite(alt)) {

			lat = f->latitude;
			lon = f->longitude;
			alt = f->altMSL;
			dbus_send_va(POSITION_INTERFACE, "PositionChanged",
				     DBUS_TYPE_DOUBLE, &lat,
				     DBUS_TYPE_DOUBLE, &lon,
				     DBUS_TYPE_DOUBLE, &alt, DBUS_TYPE_INVALID);
		}
	}

	if (isfinite(f->speed) || isfinite(f->track) || isfinite(f->climb)) {
		if (!isequal(spd, f->speed) || !isequal(trk, f->track)
		    || !isequal(clb, f->climb)
		    || !isfinite(spd) || !isfinite(trk) || !isfinite(clb)) {

			spd = f->speed;
			trk = f->track;
			clb = f->climb;
			dbus_send_va(COURSE_INTERFACE, "CourseChanged",
				     DBUS_TYPE_DOUBLE, &spd,
				     DBUS_TYPE_DOUBLE, &trk,
				     DBUS_TYPE_DOUBLE, &clb, DBUS_TYPE_INVALID);
		}
	}

	if (isfinite(f->ept) || isfinite(f->epv) || isfinite(f->epd)
	    || isfinite(f->eps) || isfinite(f->epc) || isfinite(f->eph)) {
		if (!isequal(ept, f->ept) || !isequal(epv, f->epv)
		    || !isequal(epd, f->epd) || !isequal(eps, f->eps)
		    || !isequal(epc, f->epc) || !isequal(eph, f->eph)) {

			ept = f->ept;	/* Expected time uncertainty, seconds */
			epv = f->epv;	/* Vertical pos uncertainty, meters */
			epd = f->epd;	/* Track uncertainty, degrees */
			eps = f->eps;	/* Speed uncertainty, meters/sec */
			epc = f->epc;	/* Vertical speed uncertainty */
			eph = f->eph;	/* Horizontal pos uncertainty (2D) */
			dbus_send_va(ACCURACY_INTERFACE, "AccuracyChanged",
				     DBUS_TYPE_DOUBLE, &ept,
				     DBUS_TYPE_DOUBLE, &epv,
				     DBUS_TYPE_DOUBLE, &epd,
				     DBUS_TYPE_DOUBLE, &eps,
				     DBUS_TYPE_DOUBLE, &epc,
				     DBUS_TYPE_DOUBLE, &eph, DBUS_TYPE_INVALID);
		}
	}

	return TRUE;
}

int acquire_flock(gpointer lockfd)
{
	if (flock(GPOINTER_TO_INT(lockfd), LOCK_EX|LOCK_NB) == 0) {
		g_debug("Acquired exclusive lock. Exiting.");
		flock(GPOINTER_TO_INT(lockfd), LOCK_UN);
		close(GPOINTER_TO_INT(lockfd));
		unlink(FLOCK_PATH);
		g_main_loop_quit(mainloop);
		return FALSE;
	}
	return TRUE;
}

int main(int argc, char *argv[])
{
	/* gpsd poll interval in seconds. Think about higher resolution. */
	unsigned int interval = 1;
	int lockfd = -1;

	mainloop = g_main_loop_new(NULL, FALSE);

	g_unix_signal_add(SIGHUP, sighandler, GINT_TO_POINTER(SIGHUP));
	g_unix_signal_add(SIGINT, sighandler, GINT_TO_POINTER(SIGINT));
	g_unix_signal_add(SIGTERM, sighandler, GINT_TO_POINTER(SIGTERM));

	dbus_error_init(&err);

	dbus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
	if (dbus_error_is_set(&err)) {
		g_error("DBus connection error (%s)", err.message);
		return 1;
	}

	if (dbus == NULL)
		return 1;

	ret = dbus_bus_request_name(dbus, DBUS_SERVICE,
				    DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
	if (dbus_error_is_set(&err)) {
		g_error("Name error (%s)", err.message);
		dbus_error_free(&err);
	}

	if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) {
		return 1;
	}

	lockfd = open(FLOCK_PATH, O_RDONLY, S_IWUSR|S_IRUSR|S_IWGRP|S_IRGRP);
	if (lockfd < 0) {
		g_error("open() lockfd: %s", g_strerror(errno));
	}

	if (gps_open(GPSD_HOST, GPSD_PORT, &gpsdata)) {
		g_error("Could not open gpsd socket: %d, %s", errno, gps_errstr(errno));
		return 1;
	}

	(void)gps_stream(&gpsdata, WATCH_ENABLE, NULL);

	mainloop = g_main_loop_new(context, FALSE);

	g_timeout_add_seconds(interval, poll_and_publish_gpsd_data, NULL);
	g_timeout_add_seconds(15, acquire_flock, GINT_TO_POINTER(lockfd));
	g_main_loop_run(mainloop);

	(void)gps_stream(&gpsdata, WATCH_DISABLE, NULL);
	(void)gps_close(&gpsdata);

	return 0;
}
