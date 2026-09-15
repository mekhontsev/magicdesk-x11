package com.termux.x11;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.view.Surface;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.concurrent.FutureTask;

/** One server connection and Present queue, independently retained from its output windows. */
public final class X11Session implements AutoCloseable {
    static { System.loadLibrary("Xlorie"); }

    public interface Listener {
        void onFrame(Output output, int width, int height, boolean available);
        void onDisconnected();
    }

    private static final int BIND = 0, RESIZE = 1, POINTER = 2, KEY = 3, RELEASE = 4, FOCUS = 5;
    private final HandlerThread thread = new HandlerThread("X11Session");
    private final Handler handler;
    private final Executor callbacks;
    private final Listener listener;
    private final Map<Integer, Output> outputs = new LinkedHashMap<>();
    private volatile boolean closed;
    private boolean connected;
    private int nextOutputId;
    private long nativeHandle;

    public X11Session(Executor callbacks, Listener listener) {
        this.callbacks = java.util.Objects.requireNonNull(callbacks);
        this.listener = java.util.Objects.requireNonNull(listener);
        thread.start();
        handler = new Handler(thread.getLooper());
        try {
            call(() -> {
                nativeHandle = nativeCreate();
                if (nativeHandle == 0) throw new IllegalStateException("Cannot initialize X11 renderer");
                return null;
            });
        } catch (RuntimeException e) {
            thread.quitSafely();
            throw e;
        }
    }

    /** Takes ownership of the descriptor supplied by this session's server. */
    public void connect(ParcelFileDescriptor descriptor) {
        java.util.Objects.requireNonNull(descriptor);
        try (descriptor) {
            call(() -> {
                if (!nativeConnect(nativeHandle, descriptor.detachFd())) {
                    connected = false;
                    throw new IllegalStateException("Cannot connect X11 server");
                }
                connected = true;
                for (Output output : outputs.values()) {
                    output.frameAvailable = -1;
                    output.send(BIND, 0, 0, 0, false);
                    if (output.width > 0) output.send(RESIZE, output.width, output.height, 0, false);
                }
                return null;
            });
        } catch (java.io.IOException e) {
            throw new IllegalStateException("Cannot close X11 connection descriptor", e);
        }
    }

    /** XID zero selects the whole X screen, not an Android display ID. */
    public Output openOutput(long windowId) {
        if (windowId < 0 || windowId > 0xffffffffL) throw new IllegalArgumentException("Invalid X11 window ID");
        return call(() -> {
            if (nextOutputId == Integer.MAX_VALUE) throw new IllegalStateException("Output identifiers exhausted");
            Output output = new Output(++nextOutputId, (int)windowId);
            nativeSurface(nativeHandle, output.id, null, false);
            outputs.put(output.id, output);
            output.send(BIND, 0, 0, 0, false);
            return output;
        });
    }

    public final class Output implements AutoCloseable {
        private final int id, window;
        private volatile boolean released;
        private int width, height;
        private int frameWidth = -1, frameHeight = -1, frameAvailable = -1;

        private Output(int id, int window) { this.id = id; this.window = window; }
        public int id() { return id; }
        public long windowId() { return Integer.toUnsignedLong(window); }

        public void setSurface(Surface surface, int width, int height) {
            if (surface != null && (width < 1 || height < 1 || width > 16384 || height > 16384))
                throw new IllegalArgumentException("Invalid X11 output size");
            if (released || closed) return;
            call(() -> {
                if (released) return null;
                if (surface != null) {
                    this.width = width;
                    this.height = height;
                    send(RESIZE, width, height, 0, false);
                }
                nativeSurface(nativeHandle, id, surface, false);
                return null;
            });
        }

        /** Coordinates are normalized to the displayed X11 content, excluding letterboxing. */
        public void pointer(float x, float y, int button, boolean down) {
            if (!Float.isFinite(x) || !Float.isFinite(y) || button < 0 || button > 7)
                throw new IllegalArgumentException("Invalid pointer event");
            input(POINTER, Math.round(Math.max(0, Math.min(1, x)) * 10000),
                    Math.round(Math.max(0, Math.min(1, y)) * 10000), button, down);
        }

        public void key(int androidKeyCode, int scanCode, boolean down) {
            if (androidKeyCode < 0 || scanCode < 0 || scanCode > 247)
                throw new IllegalArgumentException("Invalid keyboard event");
            input(KEY, androidKeyCode, 0, scanCode > 0 ? scanCode + 8 : 0, down);
        }

        public void focus() { input(FOCUS, 0, 0, 0, false); }

        public void text(String text) {
            java.util.Objects.requireNonNull(text);
            if (released || closed) return;
            handler.post(() -> {
                if (!released && !closed && connected) {
                    nativeText(nativeHandle, id, window, text);
                }
            });
        }

        private void input(int operation, int x, int y, int detail, boolean down) {
            if (released || closed) return;
            handler.post(() -> { if (!released && !closed) send(operation, x, y, detail, down); });
        }

        private void send(int operation, int x, int y, int detail, boolean down) {
            if (connected) nativeCommand(nativeHandle, id, window, operation, x, y, detail, down);
        }

        /** Releases only this presentation, never the X client or server process. */
        @Override public void close() {
            if (released || closed) return;
            call(() -> {
                if (!released) {
                    released = true;
                    send(RELEASE, 0, 0, 0, false);
                    nativeSurface(nativeHandle, id, null, true);
                    outputs.remove(id);
                }
                return null;
            });
        }
    }

    private void onNativeFrame(int id, int window, int width, int height, int available) {
        Output output = outputs.get(id);
        if (output == null || output.window != window) return;
        if (output.frameWidth == width && output.frameHeight == height && output.frameAvailable == available) return;
        output.frameWidth = width;
        output.frameHeight = height;
        output.frameAvailable = available;
        callbacks.execute(() -> {
            if (!closed && !output.released) listener.onFrame(output, width, height, available != 0);
        });
    }

    private void onNativeDisconnected() {
        connected = false;
        callbacks.execute(() -> { if (!closed) listener.onDisconnected(); });
    }

    @Override public void close() {
        if (closed) return;
        dispatch(() -> {
            if (closed) return null;
            for (Output output : outputs.values()) output.released = true;
            outputs.clear();
            nativeDestroy(nativeHandle);
            nativeHandle = 0;
            closed = true;
            thread.quitSafely();
            return null;
        });
    }

    private <T> T call(Callable<T> action) {
        return dispatch(() -> {
            if (closed) throw new IllegalStateException("X11 session is closed");
            return action.call();
        });
    }

    private <T> T dispatch(Callable<T> action) {
        FutureTask<T> task = new FutureTask<>(action);
        if (Looper.myLooper() == handler.getLooper()) task.run();
        else if (!handler.post(task)) throw new IllegalStateException("X11 session stopped");
        boolean interrupted = false;
        try {
            // A submitted native operation owns resources until its acknowledgement, even if the caller is interrupted.
            for (;;) {
                try { return task.get(); }
                catch (InterruptedException e) { interrupted = true; }
            }
        } catch (ExecutionException e) {
            throw new IllegalStateException("X11 operation failed", e.getCause());
        } finally {
            if (interrupted) Thread.currentThread().interrupt();
        }
    }

    private native long nativeCreate();
    private static native boolean nativeConnect(long handle, int fd);
    private static native void nativeSurface(long handle, int output, Surface surface, boolean release);
    private static native void nativeCommand(long handle, int output, int window, int operation, int x, int y, int detail, boolean down);
    private static native void nativeText(long handle, int output, int window, String text);
    private static native void nativeDestroy(long handle);
}
