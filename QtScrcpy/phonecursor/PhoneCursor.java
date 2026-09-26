package com.qtscrcpy.cursor;

import java.io.File;
import java.lang.reflect.Constructor;

/** A display-only surface, owned by this ADB session. Never injects input. */
public final class PhoneCursor {
    private static volatile String pending;
    private static long pendingReceived;
    private static volatile boolean closed;
    private static volatile long lastInput = System.nanoTime();
    private static Object call(Object target, String name, Class<?>[] types, Object... args) throws Exception {
        return target.getClass().getMethod(name, types).invoke(target, args);
    }
    private static final Class<?>[] NONE = {};
    private Object layer, surface, tx;
    private Class<?> control;

    private void create() throws Exception {
        control = Class.forName("android.view.SurfaceControl");
        Object builder = Class.forName("android.view.SurfaceControl$Builder").getDeclaredConstructor().newInstance();
        call(builder, "setName", new Class<?>[]{String.class}, "QtScrcpy phone cursor");
        call(builder, "setBufferSize", new Class<?>[]{int.class, int.class}, 64, 72);
        call(builder, "setFormat", new Class<?>[]{int.class}, -3); // TRANSLUCENT
        call(builder, "setHidden", new Class<?>[]{boolean.class}, true);
        layer = call(builder, "build", NONE);
        tx = Class.forName("android.view.SurfaceControl$Transaction").getDeclaredConstructor().newInstance();
        call(tx, "setLayer", new Class<?>[]{control, int.class}, layer, Integer.MAX_VALUE - 8);
        Class<?> surfaceClass = Class.forName("android.view.Surface");
        Constructor<?> constructor = surfaceClass.getDeclaredConstructor(control);
        constructor.setAccessible(true);
        surface = constructor.newInstance(layer);
        Object canvas = call(surface, "lockCanvas", new Class<?>[]{Class.forName("android.graphics.Rect")}, new Object[]{null});
        try {
            Class<?> mode = Class.forName("android.graphics.PorterDuff$Mode");
            call(canvas, "drawColor", new Class<?>[]{int.class, mode}, 0, mode.getField("CLEAR").get(null));
            Class<?> paintClass = Class.forName("android.graphics.Paint");
            Object paint = paintClass.getDeclaredConstructor().newInstance();
            call(paint, "setAntiAlias", new Class<?>[]{boolean.class}, true);
            // The arrow tip (6,5), rather than its bounding-box centre, is the hotspot.
            Class<?> pathClass = Class.forName("android.graphics.Path");
            Object path = pathClass.getDeclaredConstructor().newInstance();
            call(path, "moveTo", new Class<?>[]{float.class, float.class}, 6f, 5f);
            float[][] points = {{6,52},{18,41},{28,63},{38,58},{28,36},{48,36}};
            for (float[] point : points) call(path, "lineTo", new Class<?>[]{float.class, float.class}, point[0], point[1]);
            call(path, "close", NONE);
            Class<?> style = Class.forName("android.graphics.Paint$Style");
            Class<?> join = Class.forName("android.graphics.Paint$Join");
            call(paint, "setStrokeJoin", new Class<?>[]{join}, join.getField("ROUND").get(null));
            call(paint, "setStrokeWidth", new Class<?>[]{float.class}, 5f);
            call(paint, "setStyle", new Class<?>[]{style}, style.getField("FILL_AND_STROKE").get(null));
            call(paint, "setColor", new Class<?>[]{int.class}, 0xff111111);
            call(canvas, "drawPath", new Class<?>[]{pathClass, paintClass}, path, paint);
            call(paint, "setStyle", new Class<?>[]{style}, style.getField("FILL").get(null));
            call(paint, "setColor", new Class<?>[]{int.class}, 0xffffffff);
            call(canvas, "drawPath", new Class<?>[]{pathClass, paintClass}, path, paint);
        } finally {
            call(surface, "unlockCanvasAndPost", new Class<?>[]{Class.forName("android.graphics.Canvas")}, canvas);
        }
        hide();
    }

    private void hide() throws Exception {
        call(tx, "hide", new Class<?>[]{control}, layer);
        call(tx, "apply", NONE);
    }

    private void position(String line) throws Exception {
        String[] p = line.split(" ");
        if (p.length != 6 || !p[0].equals("P")) throw new IllegalArgumentException("invalid command");
        int x = Integer.parseInt(p[2]), y = Integer.parseInt(p[3]);
        int fw = Integer.parseInt(p[4]), fh = Integer.parseInt(p[5]);
        if (x < 0 || x > 1000000 || y < 0 || y > 1000000 || fw < 2 || fh < 2 || fw > 32768 || fh > 32768)
            throw new IllegalArgumentException("invalid coordinates");
        Object manager = Class.forName("android.hardware.display.DisplayManagerGlobal").getMethod("getInstance").invoke(null);
        Object info = call(manager, "getDisplayInfo", new Class<?>[]{int.class}, 0);
        if (info == null) throw new IllegalStateException("main display unavailable");
        int width = info.getClass().getField("logicalWidth").getInt(info);
        int height = info.getClass().getField("logicalHeight").getInt(info);
        int stack = info.getClass().getField("layerStack").getInt(info);
        // During a phone rotation, don't draw using a frame from the old axis.
        double error = Math.abs((double) width * fh / ((double) height * fw) - 1.0);
        if (width < 2 || height < 2 || error > Math.min(0.03, 8.0 / Math.min(fw, fh))) { hide(); return; }
        call(tx, "setLayerStack", new Class<?>[]{control, int.class}, layer, stack);
        call(tx, "setPosition", new Class<?>[]{control, float.class, float.class}, layer,
             (float) (x / 1000000.0 * (width - 1) - 6), (float) (y / 1000000.0 * (height - 1) - 5));
        call(tx, "show", new Class<?>[]{control}, layer);
        call(tx, "apply", NONE);
    }

    private void destroy() {
        try {
            if (tx != null && layer != null) {
                call(tx, "hide", new Class<?>[]{control}, layer);
                call(tx, "remove", new Class<?>[]{control}, layer);
                call(tx, "apply", NONE);
            }
        } catch (Exception ignored) { /* Binder also releases surfaces on process death. */ }
        try { if (surface != null) call(surface, "release", NONE); } catch (Exception ignored) { }
        try { if (layer != null) call(layer, "release", NONE); } catch (Exception ignored) { }
        try { if (tx != null) call(tx, "close", NONE); } catch (Exception ignored) { }
    }

    public static void main(String[] args) {
        PhoneCursor cursor = new PhoneCursor();
        try {
            // Unique, generated filename passed by the host. Never delete another session's file.
            if (args.length == 1 && args[0].matches("/data/local/tmp/qtscrcpy-cursor-[0-9a-f]{32}\\.jar"))
                new File(args[0]).delete(); // Dex has loaded; also covers a later cable/network loss.
            cursor.create();
            Thread reader = new Thread(() -> {
                try {
                    StringBuilder line = new StringBuilder();
                    for (int c; (c = System.in.read()) != -1;) {
                        if (c == '\n') {
                            String command = line.toString(); line.setLength(0);
                            if (command.equals("Q")) break;
                            synchronized (PhoneCursor.class) {
                                pending = command; pendingReceived = lastInput = System.nanoTime();
                                PhoneCursor.class.notifyAll();
                            }
                        } else {
                            if (c < 32 || c > 126 || line.length() >= 100) break;
                            line.append((char)c);
                        }
                    }
                } catch (Exception ignored) { }
                closed = true;
                synchronized (PhoneCursor.class) { PhoneCursor.class.notifyAll(); }
            }, "cursor-input");
            reader.setDaemon(true); reader.start();
            System.out.println("READY 2"); System.out.flush();
            boolean hiddenForTimeout = false;
            long sequence = 0;
            while (!closed) {
                String command;
                long received;
                synchronized (PhoneCursor.class) {
                    if (pending == null && !closed) PhoneCursor.class.wait(100);
                    command = pending; received = pendingReceived; pending = null;
                }
                long age = (System.nanoTime() - lastInput) / 1000000;
                if (age > 5000) break;
                if (age > 1200) {
                    if (!hiddenForTimeout) cursor.hide();
                    hiddenForTimeout = true;
                } else if (command != null) {
                    String[] fields = command.split(" ");
                    if (fields.length < 2) throw new IllegalArgumentException("missing sequence");
                    long current = Long.parseLong(fields[1]);
                    if (current <= sequence) throw new IllegalArgumentException("stale sequence");
                    sequence = current;
                    if (fields.length == 2 && fields[0].equals("H")) cursor.hide(); else cursor.position(command);
                    hiddenForTimeout = false;
                    // Monotonic receive/apply times also allow non-photometric latency diagnostics.
                    System.out.println("OK " + current + " " + received + " " + System.nanoTime()); System.out.flush();
                }
            }
        } catch (Throwable error) {
            System.out.println("ERROR " + error.getClass().getSimpleName());
            error.printStackTrace(System.err);
        } finally { cursor.destroy(); }
        System.exit(0); // Android Binder threads must not keep this helper alive.
    }
}
