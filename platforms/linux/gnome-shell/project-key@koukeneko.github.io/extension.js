// Shows the windows Project Key draws (its candidate window, Tab hint and 中／英 badge)
// at the caret. GNOME gives an input method no way to place a window there, so
// Project Key, inside Fcitx5, draws them itself (platforms/linux/Sources/GnomeScreen.cpp)
// and sends the pixels here, in screen coordinates. In return this tells it where the
// focused window and the screen's work area are, which turn the caret a GNOME app
// reports (relative to its window) into screen coordinates.
import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import Shell from 'gi://Shell';
import Cogl from 'gi://Cogl';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const NAME = 'org.projectkey.Panel';
// How far the blur spreads, in logical pixels: about what KWin's blur gives on KDE.
const BLUR_RADIUS = 30;
const PANEL = `<node>
  <interface name="org.projectkey.Panel1">
    <method name="Show">
      <arg type="s" name="id" direction="in"/>
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
      <arg type="i" name="width" direction="in"/>
      <arg type="i" name="height" direction="in"/>
      <arg type="s" name="pixels" direction="in"/>
      <arg type="b" name="blur" direction="in"/>
      <arg type="i" name="radius" direction="in"/>
    </method>
    <method name="Hide">
      <arg type="s" name="id" direction="in"/>
    </method>
    <method name="Reactive">
      <arg type="s" name="id" direction="in"/>
    </method>
    <method name="Opacity">
      <arg type="s" name="id" direction="in"/>
      <arg type="i" name="opacity" direction="in"/>
    </method>
    <method name="Refresh"/>
  </interface>
</node>`;

export default class ProjectKeyExtension extends Extension {
    enable() {
        this._windows = new Map();
        this._reactive = new Set();
        this._opacity = new Map();
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
    // blur asks for what is behind the window, inside its corners of radius (in pixels), blurred.
    Show(id, x, y, width, height, pixels, blur, radius) {
        // Only Project Key's own frames: this must not show any other file of the user's.
        if (!pixels.startsWith(`${GLib.get_user_runtime_dir()}/project-key-`) || pixels.includes('/..'))
            return;
        let actor = this._windows.get(id);
        if (!actor) {
            actor = new St.Widget({reactive: this._reactive.has(id), can_focus: false, track_hover: false, opacity: this._opacity.get(id) ?? 255});
            actor.connect('button-release-event', (_, event) => this._clicked(id, actor, event));
            actor.connect('motion-event', (_, event) => this._moved(id, actor, event));
            actor.connect('leave-event', () => this._send('Moved', id, -1, -1));
            actor._blur = new St.Widget();
            actor._image = new St.Widget();
            actor.add_child(actor._blur);
            actor.add_child(actor._image);
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
        actor._image.set_content(content);
        actor._image.set_size(width / scale, height / scale);
        this._shapeBlur(actor, blur ? Math.round(width / scale) : 0, Math.round(height / scale), radius / scale);
        actor.set_position(x, y);
        Main.layoutManager.uiGroup.set_child_above_sibling(actor, null);
        actor.show();
    }

    // Shell.BlurEffect blurs a whole rectangle, so the blur is a stack of strips that step in
    // at the corners, a strip for each run of rows set in alike, and none shows outside them.
    _shapeBlur(actor, width, height, radius) {
        const shape = `${width}x${height}/${radius}`;
        if (actor._shape === shape)
            return;
        actor._shape = shape;
        actor._blur.destroy_all_children();
        if (!width)
            return;
        radius = Math.min(Math.round(radius), Math.floor(width / 2), Math.floor(height / 2));
        const strip = (x, y, w, h) => {
            const effect = new Shell.BlurEffect({mode: Shell.BlurMode.BACKGROUND, brightness: 1});
            if ('radius' in effect)
                effect.radius = BLUR_RADIUS;
            else
                effect.sigma = BLUR_RADIUS / 2;  // GNOME 45
            const part = new St.Widget({x, y, width: w, height: h});
            part.add_effect(effect);
            actor._blur.add_child(part);
        };
        const runs = [];
        for (let y = 0; y < radius; y++) {
            const dy = radius - y - 0.5;
            const inset = Math.round(radius - Math.sqrt(radius * radius - dy * dy));
            if (runs.length && runs.at(-1).inset === inset)
                runs.at(-1).height++;
            else
                runs.push({y, inset, height: 1});
        }
        for (const {y, inset, height: rows} of runs) {
            strip(inset, y, width - 2 * inset, rows);
            strip(inset, height - y - rows, width - 2 * inset, rows);
        }
        strip(0, radius, width, height - 2 * radius);
    }

    Hide(id) {
        this._windows.get(id)?.hide();
    }

    // D-Bus: clicks on this window go to Project Key, not to what is under it.
    Reactive(id) {
        this._reactive.add(id);
        const actor = this._windows.get(id);
        if (actor)
            actor.reactive = true;
    }

    // D-Bus: the whole window's opacity, 0 to 255, its blur included.
    Opacity(id, opacity) {
        this._opacity.set(id, opacity);
        const actor = this._windows.get(id);
        if (actor)
            actor.opacity = opacity;
    }

    _clicked(id, actor, event) {
        if (event.get_button() !== Clutter.BUTTON_PRIMARY)
            return Clutter.EVENT_PROPAGATE;
        this._send('Clicked', id, ...this._pixels(actor, event));
        return Clutter.EVENT_STOP;
    }

    // The pointer over the window, so Project Key draws the candidate under it highlighted.
    _moved(id, actor, event) {
        this._send('Moved', id, ...this._pixels(actor, event));
        return Clutter.EVENT_PROPAGATE;
    }

    // An event's place in pixels from the window's corner.
    _pixels(actor, event) {
        const [, x, y] = actor.transform_stage_point(...event.get_coords());
        const scale = St.ThemeContext.get_for_stage(global.stage).scale_factor;
        return [Math.round(x * scale), Math.round(y * scale)];
    }

    _send(method, id, x, y) {
        Gio.DBus.session.call('org.fcitx.Fcitx5', '/projectkey/panel', 'org.projectkey.PanelHost1', method,
            new GLib.Variant('(sii)', [id, x, y]), null, Gio.DBusCallFlags.NO_AUTO_START, -1, null, null);
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
