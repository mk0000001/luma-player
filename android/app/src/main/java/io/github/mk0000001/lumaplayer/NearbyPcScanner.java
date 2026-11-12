package io.github.mk0000001.lumaplayer;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import java.util.ArrayDeque;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;

// NearbyConnection checks the applicable Bluetooth permissions before starting.
@SuppressLint("MissingPermission")
final class NearbyPcScanner implements AutoCloseable {
    private static final UUID SERVICE = UUID.fromString("32f4e091-3797-4be3-aed3-d83d5842a401");
    private static final UUID DESCRIPTOR = UUID.fromString("32f4e092-3797-4be3-aed3-d83d5842a401");
    private static final long SCAN_TIMEOUT_MS = 4000;
    private static final long READ_TIMEOUT_MS = 2500;
    private static final int MAX_DEVICES = 4;

    private final Context context;
    private final Consumer<String> onSsid;
    private final Runnable onFinished;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ArrayDeque<BluetoothDevice> devices = new ArrayDeque<>();
    private final Set<BluetoothDevice> seen = new HashSet<>();
    private final Runnable scanTimeout = this::finishScan;
    private final Runnable readTimeout = this::finishRead;
    private BluetoothLeScanner scanner;
    private BluetoothGatt activeGatt;
    private boolean scanning;
    private boolean closed;

    NearbyPcScanner(Context context, Consumer<String> onSsid, Runnable onFinished) {
        this.context = context.getApplicationContext();
        this.onSsid = onSsid;
        this.onFinished = onFinished;
    }

    boolean start() {
        try {
            BluetoothManager manager = context.getSystemService(BluetoothManager.class);
            BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
            scanner = adapter == null ? null : adapter.getBluetoothLeScanner();
            if (scanner == null) {
                return false;
            }
            ScanFilter filter = new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE)).build();
            ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
            scanner.startScan(Collections.singletonList(filter), settings, scanCallback);
            scanning = true;
            handler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS);
            return true;
        } catch (IllegalStateException | SecurityException e) {
            close();
            return false;
        }
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            handler.post(() -> {
                if (scanning && seen.add(result.getDevice())) {
                    devices.add(result.getDevice());
                    if (devices.size() == MAX_DEVICES) {
                        finishScan();
                    }
                }
            });
        }

        @Override
        public void onScanFailed(int errorCode) {
            handler.post(NearbyPcScanner.this::finishScan);
        }
    };

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            handler.post(() -> {
                if (activeGatt != gatt) {
                    return;
                }
                try {
                    if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED
                            && gatt.discoverServices()) {
                        return;
                    }
                } catch (SecurityException ignored) {
                    // Permission or Bluetooth was revoked after the foreground scan.
                }
                finishRead();
            });
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            handler.post(() -> {
                if (activeGatt != gatt) {
                    return;
                }
                BluetoothGattService service = gatt.getService(SERVICE);
                BluetoothGattCharacteristic characteristic = service == null ? null : service.getCharacteristic(DESCRIPTOR);
                try {
                    if (status == BluetoothGatt.GATT_SUCCESS && characteristic != null
                            && characteristic.getProperties() == BluetoothGattCharacteristic.PROPERTY_READ
                            && gatt.readCharacteristic(characteristic)) {
                        return;
                    }
                } catch (SecurityException ignored) {
                    // Permission or Bluetooth was revoked after the foreground scan.
                }
                finishRead();
            });
        }

        @Override
        public void onCharacteristicRead(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic,
                                         byte[] value, int status) {
            descriptorRead(gatt, characteristic, value, status);
        }

        @Override
        @SuppressWarnings("deprecation") // Android 10–12L use the characteristic's value.
        public void onCharacteristicRead(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            if (Build.VERSION.SDK_INT < 33) {
                descriptorRead(gatt, characteristic, characteristic.getValue(), status);
            }
        }
    };

    private void descriptorRead(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, byte[] value, int status) {
        String ssid = status == BluetoothGatt.GATT_SUCCESS && DESCRIPTOR.equals(characteristic.getUuid())
                ? NearbyCredentials.advertisedSsid(value) : null;
        handler.post(() -> {
            if (activeGatt == gatt) {
                if (ssid != null) {
                    onSsid.accept(ssid);
                }
                finishRead();
            }
        });
    }

    private void finishScan() {
        if (scanning) {
            stopScan();
            readNext();
        }
    }

    private void readNext() {
        while (!closed && !devices.isEmpty()) {
            try {
                activeGatt = devices.remove().connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
                if (activeGatt != null) {
                    handler.postDelayed(readTimeout, READ_TIMEOUT_MS);
                    return;
                }
            } catch (IllegalStateException | SecurityException ignored) {
                // The radio or permission can change between scan and connection.
            }
        }
        if (!closed) {
            close();
            onFinished.run();
        }
    }

    private void finishRead() {
        closeGatt();
        readNext();
    }

    private void stopScan() {
        handler.removeCallbacks(scanTimeout);
        if (scanning) {
            scanning = false;
            try {
                scanner.stopScan(scanCallback);
            } catch (IllegalStateException | SecurityException ignored) {
                // The platform has already stopped or invalidated this scan.
            }
        }
        scanner = null;
    }

    private void closeGatt() {
        handler.removeCallbacks(readTimeout);
        BluetoothGatt gatt = activeGatt;
        activeGatt = null;
        if (gatt != null) {
            try {
                gatt.close();
            } catch (SecurityException ignored) {
                // The platform has already invalidated this connection.
            }
        }
    }

    @Override
    public void close() {
        closed = true;
        stopScan();
        closeGatt();
        devices.clear();
        seen.clear();
    }
}
