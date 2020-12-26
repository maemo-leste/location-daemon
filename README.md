location-daemon
===============

Events can be seen with:

```
dbus-monitor --system path=/org/maemo/LocationDaemon
```

Spec
----

### `org.maemo.LocationDaemon.Device`

* Signal: `FixStatusChanged`
* Value type: `DBUS_TYPE_BYTE`
* Value alias: `mode`

Contains a byte: 0, 1, 2, or 3, which translates to `MODE_NOT_SEEN`,
`MODE_NO_FIX`, `MODE_2D`, and `MODE_3D`, respectively.


### `org.maemo.LocationDaemon.Satellite`

TODO


### `org.maemo.LocationDaemon.Time`

* Signal: `TimeChanged`
* Value types: `DBUS_TYPE_INT64`, `DBUS_TYPE_INT64`
* Value aliases: `tv_sec`, `tv_nsec`

Contains the current time in form of timespec, split into two args.


### `org.maemo.LocationDaemon.Position`

* Signal: `PositionChanged`
* Value types: `DBUS_TYPE_DOUBLE`, `DBUS_TYPE_DOUBLE`, `DBUS_TYPE_DOUBLE`
* Value aliases: `lat`, `lon`, `alt`

Contains latitude, longitude, and altitude.


### `org.maemo.LocationDaemon.Course`

* Signal: `CourseChanged`
* Value types: `DBUS_TYPE_DOUBLE`, `DBUS_TYPE_DOUBLE`, `DBUS_TYPE_DOUBLE`
* Value aliases: `spd`, `trk`, `clb`

Contains speed, track, and climb.


### `org.maemo.LocationDaemon.Accuracy`

* Signal: `AccuracyChanged`
* Value types: `DBUS_TYPE_DOUBLE`, `DBUS_TYPE_DOUBLE`, `DBUS_TYPE_DOUBLE`,
  `DBUS_TYPE_DOUBLE`, `DBUS_TYPE_DOUBLE`, `DBUS_TYPE_DOUBLE`
* Value aliases: `ept`, `epv`, `epd`, `eps`, `epc`, `eph`

Contains uncertainty in order:

* Expected time uncertainty, seconds
* Vertical position uncertainty, meters
* Track uncertainty, degrees
* Speed uncertainty, meters/sec
* Vertical speed uncertainty
* Horizontal position uncertainty (2D)
