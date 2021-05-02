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
#include <math.h>
#include <sys/file.h>

#include <dbus/dbus-glib-lowlevel.h>
#include <glib-unix.h>
#include <gps.h>

/* enums */
#define GPSD_HOST "localhost"
#define GPSD_PORT "2947"

#define FLOCK_PATH "/run/lock/location-daemon.lock"

#define DAEMON_DBUS_NAME     "org.maemo.LocationDaemon"
#define DAEMON_DBUS_PATH     "/org/maemo/LocationDaemon"
#define RUNNING_INTERFACE    DAEMON_DBUS_NAME".Running"
#define ACCURACY_INTERFACE   DAEMON_DBUS_NAME".Accuracy"
#define COURSE_INTERFACE     DAEMON_DBUS_NAME".Course"
#define DEVICE_INTERFACE     DAEMON_DBUS_NAME".Device"
#define POSITION_INTERFACE   DAEMON_DBUS_NAME".Position"
#define SATELLITE_INTERFACE  DAEMON_DBUS_NAME".Satellite"
#define TIME_INTERFACE       DAEMON_DBUS_NAME".Time"

/* function declarations */
static int sighandler(gpointer);
static void dbus_send_va(const char *, const char *, int, ...);
static void debug_gpsdata(struct gps_fix_t *);
static void *poll_gpsd(gpointer);
static int acquire_flock(gpointer);

/* variables */
static GMainLoop *mainloop;
static GThread *poll_thread;
static DBusConnection *dbus;
static struct gps_data_t gpsdata;
/* static struct satellite_t skyview[MAXCHANNELS]; */
static int running = 0;

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

void dbus_send_va(const char *interface, const char *sig, int f, ...)
{
	DBusMessage *msg;
	dbus_uint32_t serial = 0;

	msg = dbus_message_new_signal(DAEMON_DBUS_PATH, interface, sig);
	if (msg == NULL) {
		g_warning("dbus_message_new_signal: %s message NULL", sig);
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

void debug_gpsdata(struct gps_fix_t *f)
{
	g_debug("mode: %d", f->mode);
	g_debug("time_sec: %ld", f->time.tv_sec);
	g_debug("time_nsec: %ld", f->time.tv_nsec);
	g_debug("lat: %f", f->latitude);
	g_debug("lon: %f", f->longitude);
	g_debug("alt: %f", f->altMSL);
	g_debug("speed: %f", f->speed);
	g_debug("track: %f", f->track);
	g_debug("climb: %f", f->climb);
	g_debug("ept: %f", f->ept);
	g_debug("epv: %f", f->epv);
	g_debug("epd: %f", f->epd);
	g_debug("eps: %f", f->eps);
	g_debug("epc: %f", f->epc);
	g_debug("eph: %f", f->eph);
}

void *poll_gpsd(gpointer data)
{
	g_debug(G_STRFUNC);

	while (running) {
		if (!gps_waiting(&gpsdata, 1 * 1000000)) {
			g_debug("gps_waiting -> FALSE");
			continue;
		}

		if (gps_read(&gpsdata, NULL, 0) == -1) {
			g_warning("gpsd read error: %d, %s", errno, gps_errstr(errno));
			continue;
		}

		struct gps_fix_t *f = &gpsdata.fix;
		debug_gpsdata(f);

		switch (f->mode) {
		case MODE_NO_FIX:
		case MODE_2D:
		case MODE_3D:
			dbus_send_va(DEVICE_INTERFACE, "FixStatusChanged",
				DBUS_TYPE_BYTE, &f->mode, DBUS_TYPE_INVALID);
			break;
		default:
			continue;
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

		if (gpsdata.set & TIME_SET) {
			g_debug("TimeChanged");
			dbus_send_va(TIME_INTERFACE, "TimeChanged",
				     DBUS_TYPE_INT64, &f->time.tv_sec,
				     DBUS_TYPE_INT64, &f->time.tv_nsec,
				     DBUS_TYPE_INVALID);
		}

		if (isfinite(f->latitude) || isfinite(f->longitude)
				|| isfinite(f->altMSL)) {
			g_debug("PositionChanged");
			dbus_send_va(POSITION_INTERFACE, "PositionChanged",
				     DBUS_TYPE_DOUBLE, &f->latitude,
				     DBUS_TYPE_DOUBLE, &f->longitude,
				     DBUS_TYPE_DOUBLE, &f->altMSL,
				     DBUS_TYPE_INVALID);
		}

		if (isfinite(f->speed) || isfinite(f->track) || isfinite(f->climb)) {
			g_debug("CourseChanged");
			dbus_send_va(COURSE_INTERFACE, "CourseChanged",
				     DBUS_TYPE_DOUBLE, &f->speed,
				     DBUS_TYPE_DOUBLE, &f->track,
				     DBUS_TYPE_DOUBLE, &f->climb,
				     DBUS_TYPE_INVALID);
		}

		if (isfinite(f->ept) || isfinite(f->epv) || isfinite(f->epd)
		    || isfinite(f->eps) || isfinite(f->epc) || isfinite(f->eph)) {
			g_debug("AccuracyChanged");
			dbus_send_va(ACCURACY_INTERFACE, "AccuracyChanged",
				DBUS_TYPE_DOUBLE, &f->ept, /* Expected time uncertainty, seconds */
				DBUS_TYPE_DOUBLE, &f->epv, /* Vertical pos uncertainty, meters */
				DBUS_TYPE_DOUBLE, &f->epd, /* Track uncertainty, degrees */
				DBUS_TYPE_DOUBLE, &f->eps, /* Speed uncertainty, meters/sec */
				DBUS_TYPE_DOUBLE, &f->epc, /* Vertical speed uncertainty */
				DBUS_TYPE_DOUBLE, &f->eph, /* Horizontal pos uncertainty (2D) */
				DBUS_TYPE_INVALID);
		}
	}

	return NULL;
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
	int lockfd = -1;

	mainloop = g_main_loop_new(NULL, FALSE);

	g_unix_signal_add(SIGHUP, sighandler, GINT_TO_POINTER(SIGHUP));
	g_unix_signal_add(SIGINT, sighandler, GINT_TO_POINTER(SIGINT));
	g_unix_signal_add(SIGTERM, sighandler, GINT_TO_POINTER(SIGTERM));

	dbus = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
	if (!dbus) {
		g_critical("Failed to init DBus");
		return 1;
	}

	dbus_connection_setup_with_g_main(dbus, NULL);
	if (dbus_bus_request_name(dbus, DAEMON_DBUS_NAME, 0, NULL) != 1) {
		g_critical("Failed to register service '%s'. Already running?",
				DAEMON_DBUS_NAME);
		return 1;
	}

	lockfd = open(FLOCK_PATH, O_RDONLY, S_IWUSR|S_IRUSR|S_IWGRP|S_IRGRP);
	if (lockfd < 0) {
		g_critical("open() lockfd: %s", g_strerror(errno));
		return 1;
	}

	switch (system("sudo /etc/init.d/gpsd start")) {
	case 0:
		/* Give time to settle */
		sleep(1);
		break;
	default:
		g_critical("unable to start gpsd via initscript");
		return 1;
	}

	if (gps_open(GPSD_HOST, GPSD_PORT, &gpsdata)) {
		g_critical("Could not open gpsd socket: %s", gps_errstr(errno));
		return 1;
	}

	(void)gps_stream(&gpsdata, WATCH_ENABLE, NULL);

	running = 1;
	dbus_send_va(RUNNING_INTERFACE, "Running", DBUS_TYPE_BYTE,
		&running, DBUS_TYPE_INVALID);

	poll_thread = g_thread_new("gpsd-poll", &poll_gpsd, NULL);
	g_timeout_add_seconds(15, acquire_flock, GINT_TO_POINTER(lockfd));
	g_main_loop_run(mainloop);

	running = 0;
	g_thread_join(poll_thread);
	g_thread_unref(poll_thread);

	dbus_send_va(RUNNING_INTERFACE, "Running", DBUS_TYPE_BYTE,
		&running, DBUS_TYPE_INVALID);

	(void)gps_stream(&gpsdata, WATCH_DISABLE, NULL);
	(void)gps_close(&gpsdata);

	return 0;
}
