// Shows the windows Project Key draws (its candidate window, Tab hint and 中／英 badge)
// at the caret. GNOME gives an input method no way to place a window there, so
// Project Key, inside Fcitx5, draws them itself (platforms/linux/Sources/GnomeScreen.cpp)
// and sends the pixels here, in screen coordinates. In return this tells it where the
// focused window and the screen's work area are, which turn the caret a GNOME app
// reports (relative to its window) into screen coordinates.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import Cogl from 'gi://Cogl';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const NAME = 'org.projectkey.Panel';
const PANEL = `<node>
  <interface name="org.projectkey.Panel1">
    <method name="Show">
      <arg type="s" name="id" direction="in"/>
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
      <arg type="i" name="width" direction="in"/>
      <arg type="i" name="height" direction="in"/>
      <arg type="s" name="pixels" direction="in"/>
    </method>
    <method name="Hide">
      <arg type="s" name="id" direction="in"/>
    </method>
    <method name="Refresh"/>
  </interface>
</node>`;

export default class ProjectKeyExtension extends Extension {
    enable() {
        this._windows = new Map();
        this._panel = Gio.DBusExportedObject.wrapJSObject(PANEL, this);
        this._panel.export(Gio.DBus.session, '/org/projectkey/Panel');
        this._name = Gio.bus_own_name_on_connection(Gio.DBus.session, NAME, Gio.BusNameOwnerFlags.REPLACE, null, null);
        // Fcitx5 (re)started: it learns the geometry anew; gone: its windows go too.
        this._fcitx = Gio.bus_watch_name_on_connection(Gio.DBus.session, 'org.fcitx.Fcitx5', Gio.BusNameWatcherFlags.NONE,
            () => this._sendGeometry(), () => this._hideAll());
        this._signals = [
            [global.display, global.display.connect('notify::focus-window', () => this._watchFocus())],
            [global.display, global.display.connect('workareas-changed', () => this._sendGeometry())],
            [Main.layoutManager, Main.layoutManager.connect('monitors-changed', () => this._sendGeometry())],
        ];
        this._watchFocus();
    }

    disable() {
        this._unwatchWindow();
        for (const [object, id] of this._signals)
            object.disconnect(id);
        Gio.bus_unwatch_name(this._fcitx);
        Gio.bus_unown_name(this._name);
        this._panel.unexport();
        for (const actor of this._windows.values())
            actor.destroy();
        this._windows = null;
        this._panel = null;
    }

    // D-Bus: one of Project Key's windows, x and y in the stage's coordinates; pixels is
    // a file (in XDG_RUNTIME_DIR) of Cairo's ARGB32, premultiplied BGRA in memory, at the
    // stage's scale. A file, as a D-Bus byte array would cost Fcitx5 a call per byte.
    Show(id, x, y, width, height, pixels) {
        // Only Project Key's own frames: this must not show any other file of the user's.
        if (!pixels.startsWith(`${GLib.get_user_runtime_dir()}/project-key-`) || pixels.includes('/..'))
            return;
        let actor = this._windows.get(id);
        if (!actor) {
            actor = new St.Widget({reactive: false, can_focus: false, track_hover: false});
            Main.layoutManager.uiGroup.add_child(actor);
            this._windows.set(id, actor);
        }
        const scale = St.ThemeContext.get_for_stage(global.stage).scale_factor;
        const content = St.ImageContent.new_with_preferred_size(width / scale, height / scale);
        let bytes;
        try {
            bytes = new GLib.Bytes(GLib.file_get_contents(pixels)[1]);
        } catch {
            return;  // gone already: a newer frame is on its way
        }
        if (bytes.get_size() < width * height * 4)
            return;
        // GNOME 47 added the Cogl context as the first argument.
        if (content.set_bytes.length >= 6) {
            const context = global.stage.context.get_backend().get_cogl_context();
            content.set_bytes(context, bytes, Cogl.PixelFormat.BGRA_8888_PRE, width, height, width * 4);
        } else {
            content.set_bytes(bytes, Cogl.PixelFormat.BGRA_8888_PRE, width, height, width * 4);
        }
        actor.set_content(content);
        actor.set_position(x, y);
        actor.set_size(width / scale, height / scale);
        Main.layoutManager.uiGroup.set_child_above_sibling(actor, null);
        actor.show();
    }

    Hide(id) {
        this._windows.get(id)?.hide();
    }

    // Project Key loaded after the geometry last went out (Fcitx5 loads it when first used).
    Refresh() {
        this._sendGeometry();
    }

    _hideAll() {
        for (const actor of this._windows.values())
            actor.hide();
    }

    _watchFocus() {
        this._unwatchWindow();
        this._window = global.display.focus_window;
        if (this._window) {
            this._windowSignals = ['position-changed', 'size-changed'].map(signal =>
                this._window.connect(signal, () => this._sendGeometry()));
        }
        this._sendGeometry();
    }

    _unwatchWindow() {
        for (const id of this._windowSignals ?? [])
            this._window.disconnect(id);
        this._windowSignals = null;
        this._window = null;
    }

    // The focused window's surface (where an app's caret is measured from), the work
    // area of its monitor, and the scale, all in the stage's logical coordinates.
    _sendGeometry() {
        const window = this._window;
        const origin = window ? window.get_buffer_rect() : {x: 0, y: 0};
        const monitor = window ? window.get_monitor() : global.display.get_primary_monitor();
        const area = Main.layoutManager.getWorkAreaForMonitor(monitor);
        const scale = St.ThemeContext.get_for_stage(global.stage).scale_factor;
        Gio.DBus.session.call('org.fcitx.Fcitx5', '/projectkey/panel', 'org.projectkey.PanelHost1', 'Geometry',
            new GLib.Variant('(iiiiiii)', [origin.x, origin.y, area.x, area.y, area.width, area.height, scale]),
            null, Gio.DBusCallFlags.NO_AUTO_START, -1, null, null);
    }
}
