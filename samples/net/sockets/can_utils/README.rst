.. _can-utils-sample:

CAN utils
##########

Overview
********

The CAN utils runs on top of socket CAN and uses socket
interfaces to access the CAN bus. The below APIs are supported
and tested in this sample application. CAN STD, EXT, FD, Remote
frames are supported by these interfaces.

* candump: Display, filter and bridge CAN frames from a CAN interface
* cansend: Sends a CAN frame

CANutils can be used from user space as well as kernel space.
This app currently runs in user space.

The source code for this sample application can be found at:
:zephyr_file:`samples/net/sockets/can_utils`.

Requirements
************

You need to have socket CAN enabled

Building and Running
********************

Build the socket CAN sample application like this:

.. zephyr-app-commands::
   :zephyr-app: samples/net/sockets/can_utils

