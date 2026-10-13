#!/usr/bin/env python3
"""Test window with a configurable reaction to close requests.

usage: close-behaviour.py dialog|ignore|slow [title]
  dialog  shows a modal "save changes?" dialog and stays open
  ignore  silently refuses to close
  slow    closes 1 s after the request
"""
import sys

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk  # noqa: E402

MODE = sys.argv[1] if len(sys.argv) > 1 else "dialog"
TITLE = sys.argv[2] if len(sys.argv) > 2 else f"close-behaviour: {MODE}"


def on_delete(win, _event):
    if MODE == "ignore":
        return True
    if MODE == "slow":
        GLib.timeout_add(1000, Gtk.main_quit)
        return True
    dialog = Gtk.MessageDialog(transient_for=win, modal=True, message_type=Gtk.MessageType.QUESTION,
                               buttons=Gtk.ButtonsType.YES_NO, text="Save changes before closing?")
    if dialog.run() == Gtk.ResponseType.YES:
        Gtk.main_quit()
    dialog.destroy()
    return True


win = Gtk.Window(title=TITLE)
win.set_wmclass("close-behaviour", "close-behaviour")
win.add(Gtk.Label(label=f"mode: {MODE}"))
win.connect("delete-event", on_delete)
win.show_all()
Gtk.main()
