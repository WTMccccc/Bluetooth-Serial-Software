/*
 * winrt_abi.h - Minimal Windows Runtime (WinRT) COM ABI declarations for C.
 *
 * Windows 上访问 BLE 只有 WinRT 一套公开 API（Windows.Devices.Bluetooth.*）。
 * 这些 API 没有 C 头文件，本文件按 Windows SDK 中 MIDL 生成的 C 版 vtable
 * （windows.devices.bluetooth*.h / windows.storage.streams.h / asyncinfo.h）
 * 手工声明了本项目需要的少量接口，使主程序可以完全用 C 编写。
 *
 * 约定：
 *   - 每个接口结构体只有一个成员 lpVtbl，与 COM 完全二进制兼容。
 *   - 方法顺序与 WinRT 元数据严格一致（继承的接口方法不在同一个 vtable 里，
 *     需要 QueryInterface 拿到对应接口指针）。
 *   - HSTRING / Ro* 函数声明来自 winrt/hstring.h、winrt/roapi.h。
 */
#ifndef WINRT_ABI_H
#define WINRT_ABI_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* HSTRING                                                             */
/* ------------------------------------------------------------------ */
typedef struct HSTRING__ *HSTRING;

HRESULT WINAPI WindowsCreateString(const WCHAR *source, UINT32 len, HSTRING *out);
HRESULT WINAPI WindowsDeleteString(HSTRING string);
const WCHAR *WINAPI WindowsGetStringRawBuffer(HSTRING string, UINT32 *len);

/* ------------------------------------------------------------------ */
/* Ro* (Windows Runtime activation)                                    */
/* ------------------------------------------------------------------ */
typedef enum RO_INIT_TYPE_ {
    RO_INIT_SINGLETHREADED = 0,
    RO_INIT_MULTITHREADED  = 1
} RO_INIT_TYPE_;

HRESULT WINAPI RoInitialize(RO_INIT_TYPE_ type);
void    WINAPI RoUninitialize(void);
HRESULT WINAPI RoGetActivationFactory(HSTRING class_id, REFIID iid, void **factory);
HRESULT WINAPI RoActivateInstance(HSTRING class_id, void **instance);

typedef enum TrustLevel_ {
    BaseTrust = 0,
    PartialTrust = 1,
    FullTrust = 2
} TrustLevel_;

typedef struct EventRegistrationToken_ {
    INT64 value;
} EventRegistrationToken_;

/* ------------------------------------------------------------------ */
/* IInspectable                                                         */
/* ------------------------------------------------------------------ */
typedef struct IInspectable IInspectable;

typedef struct IInspectableVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IInspectable *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IInspectable *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IInspectable *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IInspectable *This, ULONG *iid_count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IInspectable *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IInspectable *This, TrustLevel_ *level);
} IInspectableVtbl;

struct IInspectable {
    const IInspectableVtbl *lpVtbl;
};

/* 所有 WinRT 接口 vtbl 的前 6 个方法都是 IInspectable 的六个方法 */
#define WINRT_BASE_METHODS(T)                                                   \
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(T *This, REFIID riid, void **ppv); \
    ULONG   (STDMETHODCALLTYPE *AddRef)(T *This);                               \
    ULONG   (STDMETHODCALLTYPE *Release)(T *This);                              \
    HRESULT (STDMETHODCALLTYPE *GetIids)(T *This, ULONG *iid_count, IID **iids); \
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(T *This, HSTRING *name);   \
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(T *This, TrustLevel_ *level);

/* ------------------------------------------------------------------ */
/* TypedEventHandler<T, U>（事件回调对象的 ABI）                        */
/*                                                                      */
/* 注意：WinRT 的委托对象以 IUnknown 为基类（不是 IInspectable），      */
/* vtable 只有 QueryInterface/AddRef/Release 三个方法 + Invoke。         */
/* 参见 SDK wrl/event.h 中 Delegate 标志的 static_assert。               */
/* ------------------------------------------------------------------ */
#define WINRT_DELEGATE_BASE_METHODS(T)                                          \
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(T *This, REFIID riid, void **ppv); \
    ULONG   (STDMETHODCALLTYPE *AddRef)(T *This);                               \
    ULONG   (STDMETHODCALLTYPE *Release)(T *This);

typedef struct ITypedEventHandler ITypedEventHandler;
typedef struct ITypedEventHandlerVtbl {
    WINRT_DELEGATE_BASE_METHODS(ITypedEventHandler)
    HRESULT (STDMETHODCALLTYPE *Invoke)(ITypedEventHandler *This, IInspectable *sender, IInspectable *args);
} ITypedEventHandlerVtbl;

struct ITypedEventHandler {
    const ITypedEventHandlerVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ */
/* IAsyncInfo / IAsyncOperation<T>                                      */
/*                                                                      */
/* WinRT 的泛型接口在 ABI 上都是一样的布局：                             */
/*   vtable[6] get_Completed                                          */
/*   vtable[7] put_Completed                                          */
/*   vtable[8] GetResults                                             */
/* IAsyncInfo 是另一个独立接口，用 QueryInterface 取到（IID 见 ble.c）。 */
/* ------------------------------------------------------------------ */
typedef struct IAsyncInfo IAsyncInfo;
typedef struct IAsyncInfoVtbl {
    WINRT_BASE_METHODS(IAsyncInfo)
    HRESULT (STDMETHODCALLTYPE *get_Id)(IAsyncInfo *This, UINT32 *id);
    HRESULT (STDMETHODCALLTYPE *get_Status)(IAsyncInfo *This, int *status);
    HRESULT (STDMETHODCALLTYPE *get_ErrorCode)(IAsyncInfo *This, HRESULT *error);
    HRESULT (STDMETHODCALLTYPE *Cancel)(IAsyncInfo *This);
    HRESULT (STDMETHODCALLTYPE *Close)(IAsyncInfo *This);
} IAsyncInfoVtbl;

struct IAsyncInfo {
    const IAsyncInfoVtbl *lpVtbl;
};

/* AsyncStatus */
#define ASYNC_STATUS_STARTED   0
#define ASYNC_STATUS_COMPLETED 1
#define ASYNC_STATUS_CANCELED  2
#define ASYNC_STATUS_ERROR     3

typedef struct IAsyncOperation IAsyncOperation;
typedef struct IAsyncOperationVtbl {
    WINRT_BASE_METHODS(IAsyncOperation)
    HRESULT (STDMETHODCALLTYPE *get_Completed)(IAsyncOperation *This, void **handler);
    HRESULT (STDMETHODCALLTYPE *put_Completed)(IAsyncOperation *This, void *handler);
    /* 真实返回类型由具体泛型实例决定，例如 IBluetoothLEDevice**，
       调用方使用下面 async_get_result() 里的强类型函数指针。 */
    HRESULT (STDMETHODCALLTYPE *GetResults)(IAsyncOperation *This, void *result);
} IAsyncOperationVtbl;

struct IAsyncOperation {
    const IAsyncOperationVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ */
/* IBuffer / IBufferByteAccess                                          */
/* ------------------------------------------------------------------ */
typedef struct IBuffer IBuffer;
typedef struct IBufferVtbl {
    WINRT_BASE_METHODS(IBuffer)
    HRESULT (STDMETHODCALLTYPE *get_Capacity)(IBuffer *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *get_Length)(IBuffer *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *put_Length)(IBuffer *This, UINT32 value);
} IBufferVtbl;

struct IBuffer {
    const IBufferVtbl *lpVtbl;
};

typedef struct IBufferByteAccess IBufferByteAccess;
typedef struct IBufferByteAccessVtbl {
    WINRT_BASE_METHODS(IBufferByteAccess)
    HRESULT (STDMETHODCALLTYPE *Buffer)(IBufferByteAccess *This, BYTE **value);
} IBufferByteAccessVtbl;

struct IBufferByteAccess {
    const IBufferByteAccessVtbl *lpVtbl;
};

/* Windows.Storage.Streams.IDataWriter（只声明用到的 WriteBytes / DetachBuffer） */
typedef struct IDataWriter IDataWriter;
typedef struct IDataWriterVtbl {
    WINRT_BASE_METHODS(IDataWriter)
    HRESULT (STDMETHODCALLTYPE *get_UnstoredBufferLength)(IDataWriter *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *get_UnicodeEncoding)(IDataWriter *This, int *value);
    HRESULT (STDMETHODCALLTYPE *put_UnicodeEncoding)(IDataWriter *This, int value);
    HRESULT (STDMETHODCALLTYPE *get_ByteOrder)(IDataWriter *This, int *value);
    HRESULT (STDMETHODCALLTYPE *put_ByteOrder)(IDataWriter *This, int value);
    HRESULT (STDMETHODCALLTYPE *WriteByte)(IDataWriter *This, BYTE value);
    HRESULT (STDMETHODCALLTYPE *WriteBytes)(IDataWriter *This, UINT32 size, BYTE *value);
    HRESULT (STDMETHODCALLTYPE *WriteBuffer)(IDataWriter *This, IBuffer *buffer);
    HRESULT (STDMETHODCALLTYPE *WriteBufferRange)(IDataWriter *This, IBuffer *buffer, UINT32 start, UINT32 count);
    HRESULT (STDMETHODCALLTYPE *WriteBoolean)(IDataWriter *This, BOOLEAN value);
    HRESULT (STDMETHODCALLTYPE *WriteGuid)(IDataWriter *This, GUID value);
    HRESULT (STDMETHODCALLTYPE *WriteInt16)(IDataWriter *This, INT16 value);
    HRESULT (STDMETHODCALLTYPE *WriteInt32)(IDataWriter *This, INT32 value);
    HRESULT (STDMETHODCALLTYPE *WriteInt64)(IDataWriter *This, INT64 value);
    HRESULT (STDMETHODCALLTYPE *WriteUInt16)(IDataWriter *This, UINT16 value);
    HRESULT (STDMETHODCALLTYPE *WriteUInt32)(IDataWriter *This, UINT32 value);
    HRESULT (STDMETHODCALLTYPE *WriteUInt64)(IDataWriter *This, UINT64 value);
    HRESULT (STDMETHODCALLTYPE *WriteSingle)(IDataWriter *This, float value);
    HRESULT (STDMETHODCALLTYPE *WriteDouble)(IDataWriter *This, double value);
    HRESULT (STDMETHODCALLTYPE *WriteDateTime)(IDataWriter *This, INT64 value);
    HRESULT (STDMETHODCALLTYPE *WriteTimeSpan)(IDataWriter *This, INT64 value);
    HRESULT (STDMETHODCALLTYPE *WriteString)(IDataWriter *This, HSTRING value, UINT32 *code_unit_count);
    HRESULT (STDMETHODCALLTYPE *MeasureString)(IDataWriter *This, HSTRING value, UINT32 *code_unit_count);
    HRESULT (STDMETHODCALLTYPE *StoreAsync)(IDataWriter *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *FlushAsync)(IDataWriter *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *DetachBuffer)(IDataWriter *This, IBuffer **buffer);
    HRESULT (STDMETHODCALLTYPE *DetachStream)(IDataWriter *This, void **stream);
} IDataWriterVtbl;

struct IDataWriter {
    const IDataWriterVtbl *lpVtbl;
};

/* Windows.Storage.Streams.IDataReader / IDataReaderStatics
 * 事件通知里的 IBuffer 常常是跨公寓代理，IBufferByteAccess 取不到数据，
 * 必须用 DataReader.FromBuffer() 读。 */
typedef struct IDataReader IDataReader;
typedef struct IDataReaderVtbl {
    WINRT_BASE_METHODS(IDataReader)
    HRESULT (STDMETHODCALLTYPE *get_UnconsumedBufferLength)(IDataReader *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *get_UnicodeEncoding)(IDataReader *This, int *value);
    HRESULT (STDMETHODCALLTYPE *put_UnicodeEncoding)(IDataReader *This, int value);
    HRESULT (STDMETHODCALLTYPE *get_ByteOrder)(IDataReader *This, int *value);
    HRESULT (STDMETHODCALLTYPE *put_ByteOrder)(IDataReader *This, int value);
    HRESULT (STDMETHODCALLTYPE *get_InputStreamOptions)(IDataReader *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *put_InputStreamOptions)(IDataReader *This, UINT32 value);
    HRESULT (STDMETHODCALLTYPE *ReadByte)(IDataReader *This, BYTE *value);
    HRESULT (STDMETHODCALLTYPE *ReadBytes)(IDataReader *This, UINT32 size, BYTE *value);
    HRESULT (STDMETHODCALLTYPE *ReadBuffer)(IDataReader *This, UINT32 length, IBuffer **value);
    HRESULT (STDMETHODCALLTYPE *ReadBoolean)(IDataReader *This, BOOLEAN *value);
    HRESULT (STDMETHODCALLTYPE *ReadGuid)(IDataReader *This, GUID *value);
    HRESULT (STDMETHODCALLTYPE *ReadInt16)(IDataReader *This, INT16 *value);
    HRESULT (STDMETHODCALLTYPE *ReadInt32)(IDataReader *This, INT32 *value);
    HRESULT (STDMETHODCALLTYPE *ReadInt64)(IDataReader *This, INT64 *value);
    HRESULT (STDMETHODCALLTYPE *ReadUInt16)(IDataReader *This, UINT16 *value);
    HRESULT (STDMETHODCALLTYPE *ReadUInt32)(IDataReader *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *ReadUInt64)(IDataReader *This, UINT64 *value);
    HRESULT (STDMETHODCALLTYPE *ReadSingle)(IDataReader *This, float *value);
    HRESULT (STDMETHODCALLTYPE *ReadDouble)(IDataReader *This, double *value);
    HRESULT (STDMETHODCALLTYPE *ReadString)(IDataReader *This, UINT32 code_unit_count, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *ReadDateTime)(IDataReader *This, INT64 *value);
    HRESULT (STDMETHODCALLTYPE *ReadTimeSpan)(IDataReader *This, INT64 *value);
    HRESULT (STDMETHODCALLTYPE *LoadAsync)(IDataReader *This, UINT32 count, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *DetachBuffer)(IDataReader *This, IBuffer **buffer);
    HRESULT (STDMETHODCALLTYPE *DetachStream)(IDataReader *This, void **stream);
} IDataReaderVtbl;

struct IDataReader {
    const IDataReaderVtbl *lpVtbl;
};

typedef struct IDataReaderStatics IDataReaderStatics;
typedef struct IDataReaderStaticsVtbl {
    WINRT_BASE_METHODS(IDataReaderStatics)
    HRESULT (STDMETHODCALLTYPE *FromBuffer)(IDataReaderStatics *This, IBuffer *buffer, IDataReader **reader);
} IDataReaderStaticsVtbl;

struct IDataReaderStatics {
    const IDataReaderStaticsVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ */
/* IClosable                                                            */
/* ------------------------------------------------------------------ */
typedef struct IClosable IClosable;
typedef struct IClosableVtbl {
    WINRT_BASE_METHODS(IClosable)
    HRESULT (STDMETHODCALLTYPE *Close)(IClosable *This);
} IClosableVtbl;

struct IClosable {
    const IClosableVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ */
/* IVectorView<T>                                                       */
/* ------------------------------------------------------------------ */
typedef struct IVectorView IVectorView;
typedef struct IVectorViewVtbl {
    WINRT_BASE_METHODS(IVectorView)
    HRESULT (STDMETHODCALLTYPE *GetAt)(IVectorView *This, UINT32 index, void **item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(IVectorView *This, UINT32 *size);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(IVectorView *This, void *item, UINT32 *index, BOOLEAN *found);
    HRESULT (STDMETHODCALLTYPE *GetMany)(IVectorView *This, UINT32 start, UINT32 count, void **items, UINT32 *actual);
} IVectorViewVtbl;

struct IVectorView {
    const IVectorViewVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ */
/* Windows.Devices.Bluetooth                                            */
/* ------------------------------------------------------------------ */
#define BLUETOOTH_CONNECTION_DISCONNECTED 0
#define BLUETOOTH_CONNECTION_CONNECTED    1

typedef struct IBluetoothLEDevice IBluetoothLEDevice;
typedef struct IBluetoothLEDeviceVtbl {
    WINRT_BASE_METHODS(IBluetoothLEDevice)
    HRESULT (STDMETHODCALLTYPE *get_DeviceId)(IBluetoothLEDevice *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Name)(IBluetoothLEDevice *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_GattServices)(IBluetoothLEDevice *This, IVectorView **value);
    HRESULT (STDMETHODCALLTYPE *get_ConnectionStatus)(IBluetoothLEDevice *This, int *value);
    HRESULT (STDMETHODCALLTYPE *get_BluetoothAddress)(IBluetoothLEDevice *This, UINT64 *value);
    HRESULT (STDMETHODCALLTYPE *GetGattService)(IBluetoothLEDevice *This, GUID service_uuid, void **service);
    HRESULT (STDMETHODCALLTYPE *add_NameChanged)(IBluetoothLEDevice *This, void *handler, EventRegistrationToken_ *token);
    HRESULT (STDMETHODCALLTYPE *remove_NameChanged)(IBluetoothLEDevice *This, EventRegistrationToken_ token);
    HRESULT (STDMETHODCALLTYPE *add_GattServicesChanged)(IBluetoothLEDevice *This, void *handler, EventRegistrationToken_ *token);
    HRESULT (STDMETHODCALLTYPE *remove_GattServicesChanged)(IBluetoothLEDevice *This, EventRegistrationToken_ token);
    HRESULT (STDMETHODCALLTYPE *add_ConnectionStatusChanged)(IBluetoothLEDevice *This, void *handler, EventRegistrationToken_ *token);
    HRESULT (STDMETHODCALLTYPE *remove_ConnectionStatusChanged)(IBluetoothLEDevice *This, EventRegistrationToken_ token);
} IBluetoothLEDeviceVtbl;

struct IBluetoothLEDevice {
    const IBluetoothLEDeviceVtbl *lpVtbl;
};

typedef struct IBluetoothLEDevice3 IBluetoothLEDevice3;
typedef struct IBluetoothLEDevice3Vtbl {
    WINRT_BASE_METHODS(IBluetoothLEDevice3)
    HRESULT (STDMETHODCALLTYPE *get_DeviceAccessInformation)(IBluetoothLEDevice3 *This, void **value);
    HRESULT (STDMETHODCALLTYPE *RequestAccessAsync)(IBluetoothLEDevice3 *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetGattServicesAsync)(IBluetoothLEDevice3 *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetGattServicesWithCacheModeAsync)(IBluetoothLEDevice3 *This, int cache_mode, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetGattServicesForUuidAsync)(IBluetoothLEDevice3 *This, GUID uuid, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetGattServicesForUuidWithCacheModeAsync)(IBluetoothLEDevice3 *This, GUID uuid, int cache_mode, IAsyncOperation **operation);
} IBluetoothLEDevice3Vtbl;

struct IBluetoothLEDevice3 {
    const IBluetoothLEDevice3Vtbl *lpVtbl;
};

typedef struct IBluetoothLEDeviceStatics IBluetoothLEDeviceStatics;
typedef struct IBluetoothLEDeviceStaticsVtbl {
    WINRT_BASE_METHODS(IBluetoothLEDeviceStatics)
    HRESULT (STDMETHODCALLTYPE *FromIdAsync)(IBluetoothLEDeviceStatics *This, HSTRING device_id, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *FromBluetoothAddressAsync)(IBluetoothLEDeviceStatics *This, UINT64 address, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetDeviceSelector)(IBluetoothLEDeviceStatics *This, HSTRING *result);
} IBluetoothLEDeviceStaticsVtbl;

struct IBluetoothLEDeviceStatics {
    const IBluetoothLEDeviceStaticsVtbl *lpVtbl;
};

typedef struct IBluetoothLEDeviceStatics2 IBluetoothLEDeviceStatics2;
typedef struct IBluetoothLEDeviceStatics2Vtbl {
    WINRT_BASE_METHODS(IBluetoothLEDeviceStatics2)
    HRESULT (STDMETHODCALLTYPE *GetDeviceSelectorFromPairingState)(IBluetoothLEDeviceStatics2 *This, BOOLEAN pairing_state, HSTRING *selector);
    HRESULT (STDMETHODCALLTYPE *GetDeviceSelectorFromConnectionStatus)(IBluetoothLEDeviceStatics2 *This, int connection_status, HSTRING *selector);
    HRESULT (STDMETHODCALLTYPE *GetDeviceSelectorFromDeviceName)(IBluetoothLEDeviceStatics2 *This, HSTRING device_name, HSTRING *selector);
    HRESULT (STDMETHODCALLTYPE *GetDeviceSelectorFromBluetoothAddress)(IBluetoothLEDeviceStatics2 *This, UINT64 address, HSTRING *selector);
    HRESULT (STDMETHODCALLTYPE *GetDeviceSelectorFromBluetoothAddressWithBluetoothAddressType)(IBluetoothLEDeviceStatics2 *This, UINT64 address, int address_type, HSTRING *selector);
    HRESULT (STDMETHODCALLTYPE *GetDeviceSelectorFromAppearance)(IBluetoothLEDeviceStatics2 *This, void *appearance, HSTRING *selector);
    HRESULT (STDMETHODCALLTYPE *FromBluetoothAddressWithBluetoothAddressTypeAsync)(IBluetoothLEDeviceStatics2 *This, UINT64 address, int address_type, IAsyncOperation **operation);
} IBluetoothLEDeviceStatics2Vtbl;

struct IBluetoothLEDeviceStatics2 {
    const IBluetoothLEDeviceStatics2Vtbl *lpVtbl;
};

/* ------------------------------------------------------------------ */
/* Windows.Devices.Bluetooth.Advertisement                              */
/* ------------------------------------------------------------------ */
#define BLE_SCANNING_PASSIVE 0
#define BLE_SCANNING_ACTIVE  1

typedef struct IBluetoothLEAdvertisement IBluetoothLEAdvertisement;
typedef struct IBluetoothLEAdvertisementVtbl {
    WINRT_BASE_METHODS(IBluetoothLEAdvertisement)
    HRESULT (STDMETHODCALLTYPE *get_Flags)(IBluetoothLEAdvertisement *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_Flags)(IBluetoothLEAdvertisement *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_LocalName)(IBluetoothLEAdvertisement *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *put_LocalName)(IBluetoothLEAdvertisement *This, HSTRING value);
    HRESULT (STDMETHODCALLTYPE *get_ServiceUuids)(IBluetoothLEAdvertisement *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_ManufacturerData)(IBluetoothLEAdvertisement *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_DataSections)(IBluetoothLEAdvertisement *This, void **value);
} IBluetoothLEAdvertisementVtbl;

struct IBluetoothLEAdvertisement {
    const IBluetoothLEAdvertisementVtbl *lpVtbl;
};

typedef struct IBluetoothLEAdvertisementReceivedEventArgs IBluetoothLEAdvertisementReceivedEventArgs;
typedef struct IBluetoothLEAdvertisementReceivedEventArgsVtbl {
    WINRT_BASE_METHODS(IBluetoothLEAdvertisementReceivedEventArgs)
    HRESULT (STDMETHODCALLTYPE *get_RawSignalStrengthInDBm)(IBluetoothLEAdvertisementReceivedEventArgs *This, INT16 *value);
    HRESULT (STDMETHODCALLTYPE *get_BluetoothAddress)(IBluetoothLEAdvertisementReceivedEventArgs *This, UINT64 *value);
    HRESULT (STDMETHODCALLTYPE *get_AdvertisementType)(IBluetoothLEAdvertisementReceivedEventArgs *This, int *value);
    HRESULT (STDMETHODCALLTYPE *get_Timestamp)(IBluetoothLEAdvertisementReceivedEventArgs *This, INT64 *value);
    HRESULT (STDMETHODCALLTYPE *get_Advertisement)(IBluetoothLEAdvertisementReceivedEventArgs *This, IBluetoothLEAdvertisement **value);
} IBluetoothLEAdvertisementReceivedEventArgsVtbl;

struct IBluetoothLEAdvertisementReceivedEventArgs {
    const IBluetoothLEAdvertisementReceivedEventArgsVtbl *lpVtbl;
};

typedef struct IBluetoothLEAdvertisementReceivedEventArgs2 IBluetoothLEAdvertisementReceivedEventArgs2;
typedef struct IBluetoothLEAdvertisementReceivedEventArgs2Vtbl {
    WINRT_BASE_METHODS(IBluetoothLEAdvertisementReceivedEventArgs2)
    HRESULT (STDMETHODCALLTYPE *get_BluetoothAddressType)(IBluetoothLEAdvertisementReceivedEventArgs2 *This, int *value);
    HRESULT (STDMETHODCALLTYPE *get_TransmitPowerLevelInDBm)(IBluetoothLEAdvertisementReceivedEventArgs2 *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_IsAnonymous)(IBluetoothLEAdvertisementReceivedEventArgs2 *This, BOOLEAN *value);
    HRESULT (STDMETHODCALLTYPE *get_IsConnectable)(IBluetoothLEAdvertisementReceivedEventArgs2 *This, BOOLEAN *value);
    HRESULT (STDMETHODCALLTYPE *get_IsScannable)(IBluetoothLEAdvertisementReceivedEventArgs2 *This, BOOLEAN *value);
    HRESULT (STDMETHODCALLTYPE *get_IsDirected)(IBluetoothLEAdvertisementReceivedEventArgs2 *This, BOOLEAN *value);
    HRESULT (STDMETHODCALLTYPE *get_IsScanResponse)(IBluetoothLEAdvertisementReceivedEventArgs2 *This, BOOLEAN *value);
} IBluetoothLEAdvertisementReceivedEventArgs2Vtbl;

struct IBluetoothLEAdvertisementReceivedEventArgs2 {
    const IBluetoothLEAdvertisementReceivedEventArgs2Vtbl *lpVtbl;
};

typedef struct IBluetoothLEAdvertisementWatcher IBluetoothLEAdvertisementWatcher;
typedef struct IBluetoothLEAdvertisementWatcherVtbl {
    WINRT_BASE_METHODS(IBluetoothLEAdvertisementWatcher)
    HRESULT (STDMETHODCALLTYPE *get_MinSamplingInterval)(IBluetoothLEAdvertisementWatcher *This, INT64 *value);
    HRESULT (STDMETHODCALLTYPE *get_MaxSamplingInterval)(IBluetoothLEAdvertisementWatcher *This, INT64 *value);
    HRESULT (STDMETHODCALLTYPE *get_MinOutOfRangeTimeout)(IBluetoothLEAdvertisementWatcher *This, INT64 *value);
    HRESULT (STDMETHODCALLTYPE *get_MaxOutOfRangeTimeout)(IBluetoothLEAdvertisementWatcher *This, INT64 *value);
    HRESULT (STDMETHODCALLTYPE *get_Status)(IBluetoothLEAdvertisementWatcher *This, int *value);
    HRESULT (STDMETHODCALLTYPE *get_ScanningMode)(IBluetoothLEAdvertisementWatcher *This, int *value);
    HRESULT (STDMETHODCALLTYPE *put_ScanningMode)(IBluetoothLEAdvertisementWatcher *This, int value);
    HRESULT (STDMETHODCALLTYPE *get_SignalStrengthFilter)(IBluetoothLEAdvertisementWatcher *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_SignalStrengthFilter)(IBluetoothLEAdvertisementWatcher *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_AdvertisementFilter)(IBluetoothLEAdvertisementWatcher *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_AdvertisementFilter)(IBluetoothLEAdvertisementWatcher *This, void *value);
    HRESULT (STDMETHODCALLTYPE *Start)(IBluetoothLEAdvertisementWatcher *This);
    HRESULT (STDMETHODCALLTYPE *Stop)(IBluetoothLEAdvertisementWatcher *This);
    HRESULT (STDMETHODCALLTYPE *add_Received)(IBluetoothLEAdvertisementWatcher *This, void *handler, EventRegistrationToken_ *token);
    HRESULT (STDMETHODCALLTYPE *remove_Received)(IBluetoothLEAdvertisementWatcher *This, EventRegistrationToken_ token);
    HRESULT (STDMETHODCALLTYPE *add_Stopped)(IBluetoothLEAdvertisementWatcher *This, void *handler, EventRegistrationToken_ *token);
    HRESULT (STDMETHODCALLTYPE *remove_Stopped)(IBluetoothLEAdvertisementWatcher *This, EventRegistrationToken_ token);
} IBluetoothLEAdvertisementWatcherVtbl;

struct IBluetoothLEAdvertisementWatcher {
    const IBluetoothLEAdvertisementWatcherVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ */
/* Windows.Devices.Bluetooth.GenericAttributeProfile                    */
/* ------------------------------------------------------------------ */
#define GATT_COMMUNICATION_SUCCESS        0
#define GATT_COMMUNICATION_UNREACHABLE    1
#define GATT_COMMUNICATION_PROTOCOL_ERROR 2
#define GATT_COMMUNICATION_ACCESS_DENIED  3

#define GATT_PROP_BROADCAST                   0x01
#define GATT_PROP_READ                        0x02
#define GATT_PROP_WRITE_WITHOUT_RESPONSE      0x04
#define GATT_PROP_WRITE                       0x08
#define GATT_PROP_NOTIFY                      0x10
#define GATT_PROP_INDICATE                    0x20
#define GATT_PROP_AUTHENTICATED_SIGNED_WRITES 0x40
#define GATT_PROP_EXTENDED_PROPERTIES         0x80
#define GATT_PROP_RELIABLE_WRITES             0x100
#define GATT_PROP_WRITABLE_AUXILIARIES        0x200

#define GATT_CCCD_NONE     0
#define GATT_CCCD_NOTIFY   1
#define GATT_CCCD_INDICATE 2

#define GATT_WRITE_WITH_RESPONSE    0
#define GATT_WRITE_WITHOUT_RESPONSE 1

typedef struct IGattDeviceService IGattDeviceService;
typedef struct IGattDeviceServiceVtbl {
    WINRT_BASE_METHODS(IGattDeviceService)
    HRESULT (STDMETHODCALLTYPE *GetCharacteristics)(IGattDeviceService *This, GUID uuid, IVectorView **value);
    HRESULT (STDMETHODCALLTYPE *GetIncludedServices)(IGattDeviceService *This, GUID uuid, IVectorView **value);
    HRESULT (STDMETHODCALLTYPE *get_DeviceId)(IGattDeviceService *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Uuid)(IGattDeviceService *This, GUID *value);
    HRESULT (STDMETHODCALLTYPE *get_AttributeHandle)(IGattDeviceService *This, UINT16 *value);
} IGattDeviceServiceVtbl;

struct IGattDeviceService {
    const IGattDeviceServiceVtbl *lpVtbl;
};

typedef struct IGattDeviceService3 IGattDeviceService3;
typedef struct IGattDeviceService3Vtbl {
    WINRT_BASE_METHODS(IGattDeviceService3)
    HRESULT (STDMETHODCALLTYPE *get_DeviceAccessInformation)(IGattDeviceService3 *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Session)(IGattDeviceService3 *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_SharingMode)(IGattDeviceService3 *This, int *value);
    HRESULT (STDMETHODCALLTYPE *RequestAccessAsync)(IGattDeviceService3 *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *OpenAsync)(IGattDeviceService3 *This, int sharing_mode, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetCharacteristicsAsync)(IGattDeviceService3 *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetCharacteristicsWithCacheModeAsync)(IGattDeviceService3 *This, int cache_mode, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetCharacteristicsForUuidAsync)(IGattDeviceService3 *This, GUID uuid, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetCharacteristicsForUuidWithCacheModeAsync)(IGattDeviceService3 *This, GUID uuid, int cache_mode, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetIncludedServicesAsync)(IGattDeviceService3 *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetIncludedServicesWithCacheModeAsync)(IGattDeviceService3 *This, int cache_mode, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetIncludedServicesForUuidAsync)(IGattDeviceService3 *This, GUID uuid, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *GetIncludedServicesForUuidWithCacheModeAsync)(IGattDeviceService3 *This, GUID uuid, int cache_mode, IAsyncOperation **operation);
} IGattDeviceService3Vtbl;

struct IGattDeviceService3 {
    const IGattDeviceService3Vtbl *lpVtbl;
};

typedef struct IGattDeviceServicesResult IGattDeviceServicesResult;
typedef struct IGattDeviceServicesResultVtbl {
    WINRT_BASE_METHODS(IGattDeviceServicesResult)
    HRESULT (STDMETHODCALLTYPE *get_Status)(IGattDeviceServicesResult *This, int *value);
    HRESULT (STDMETHODCALLTYPE *get_ProtocolError)(IGattDeviceServicesResult *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Services)(IGattDeviceServicesResult *This, IVectorView **value);
} IGattDeviceServicesResultVtbl;

struct IGattDeviceServicesResult {
    const IGattDeviceServicesResultVtbl *lpVtbl;
};

typedef struct IGattCharacteristic IGattCharacteristic;
typedef struct IGattCharacteristicVtbl {
    WINRT_BASE_METHODS(IGattCharacteristic)
    HRESULT (STDMETHODCALLTYPE *GetDescriptors)(IGattCharacteristic *This, GUID uuid, IVectorView **value);
    HRESULT (STDMETHODCALLTYPE *get_CharacteristicProperties)(IGattCharacteristic *This, int *value);
    HRESULT (STDMETHODCALLTYPE *get_ProtectionLevel)(IGattCharacteristic *This, int *value);
    HRESULT (STDMETHODCALLTYPE *put_ProtectionLevel)(IGattCharacteristic *This, int value);
    HRESULT (STDMETHODCALLTYPE *get_UserDescription)(IGattCharacteristic *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Uuid)(IGattCharacteristic *This, GUID *value);
    HRESULT (STDMETHODCALLTYPE *get_AttributeHandle)(IGattCharacteristic *This, UINT16 *value);
    HRESULT (STDMETHODCALLTYPE *get_PresentationFormats)(IGattCharacteristic *This, IVectorView **value);
    HRESULT (STDMETHODCALLTYPE *ReadValueAsync)(IGattCharacteristic *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *ReadValueWithCacheModeAsync)(IGattCharacteristic *This, int cache_mode, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *WriteValueAsync)(IGattCharacteristic *This, IBuffer *value, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *WriteValueWithOptionAsync)(IGattCharacteristic *This, IBuffer *value, int write_option, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *ReadClientCharacteristicConfigurationDescriptorAsync)(IGattCharacteristic *This, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *WriteClientCharacteristicConfigurationDescriptorAsync)(IGattCharacteristic *This, int value, IAsyncOperation **operation);
    HRESULT (STDMETHODCALLTYPE *add_ValueChanged)(IGattCharacteristic *This, void *handler, EventRegistrationToken_ *token);
    HRESULT (STDMETHODCALLTYPE *remove_ValueChanged)(IGattCharacteristic *This, EventRegistrationToken_ token);
} IGattCharacteristicVtbl;

struct IGattCharacteristic {
    const IGattCharacteristicVtbl *lpVtbl;
};

typedef struct IGattCharacteristicsResult IGattCharacteristicsResult;
typedef struct IGattCharacteristicsResultVtbl {
    WINRT_BASE_METHODS(IGattCharacteristicsResult)
    HRESULT (STDMETHODCALLTYPE *get_Status)(IGattCharacteristicsResult *This, int *value);
    HRESULT (STDMETHODCALLTYPE *get_ProtocolError)(IGattCharacteristicsResult *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Characteristics)(IGattCharacteristicsResult *This, IVectorView **value);
} IGattCharacteristicsResultVtbl;

struct IGattCharacteristicsResult {
    const IGattCharacteristicsResultVtbl *lpVtbl;
};

typedef struct IGattSession IGattSession;
typedef struct IGattSessionVtbl {
    WINRT_BASE_METHODS(IGattSession)
    HRESULT (STDMETHODCALLTYPE *get_DeviceId)(IGattSession *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_CanMaintainConnection)(IGattSession *This, BOOLEAN *value);
    HRESULT (STDMETHODCALLTYPE *put_MaintainConnection)(IGattSession *This, BOOLEAN value);
    HRESULT (STDMETHODCALLTYPE *get_MaintainConnection)(IGattSession *This, BOOLEAN *value);
    HRESULT (STDMETHODCALLTYPE *get_MaxPduSize)(IGattSession *This, UINT16 *value);
    HRESULT (STDMETHODCALLTYPE *get_SessionStatus)(IGattSession *This, int *value);
} IGattSessionVtbl;

struct IGattSession {
    const IGattSessionVtbl *lpVtbl;
};

typedef struct IGattValueChangedEventArgs IGattValueChangedEventArgs;
typedef struct IGattValueChangedEventArgsVtbl {
    WINRT_BASE_METHODS(IGattValueChangedEventArgs)
    HRESULT (STDMETHODCALLTYPE *get_CharacteristicValue)(IGattValueChangedEventArgs *This, IBuffer **value);
    HRESULT (STDMETHODCALLTYPE *get_Timestamp)(IGattValueChangedEventArgs *This, INT64 *value);
} IGattValueChangedEventArgsVtbl;

struct IGattValueChangedEventArgs {
    const IGattValueChangedEventArgsVtbl *lpVtbl;
};

#ifdef __cplusplus
}
#endif

#endif /* WINRT_ABI_H */
