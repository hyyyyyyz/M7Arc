// A native 7-Zip module: an XOR codec and a single-file format using the host's Copy codec.
#include "Common/MyInitGuid.h"

#include "Common/MyCom.h"
#include "7zip/ICoder.h"
#include "7zip/Archive/IArchive.h"
#include "7zip/PropID.h"

#include <array>
#include <cstring>

#define MODULE_API extern "C" __attribute__((visibility("default"))) HRESULT

namespace {
constexpr GUID codecClass{0x4d374152, 0x4301, 0x2603, {1, 0, 0, 0, 0, 0, 0, 1}};
constexpr GUID archiveClass{0x4d374152, 0x4301, 0x2603, {1, 0, 0, 0, 0, 0, 0, 2}};
constexpr UInt64 codecId = 0xF7A001;
constexpr char signature[] = "M7MOD001";
CMyComPtr<ICompressCodecsInfo> host;

HRESULT textProperty(const wchar_t* text, PROPVARIANT* value) {
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocString(text);
    return value->bstrVal ? S_OK : E_OUTOFMEMORY;
}
HRESULT bytesProperty(const void* bytes, unsigned size, PROPVARIANT* value) {
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocStringByteLen(static_cast<const char*>(bytes), size);
    return value->bstrVal ? S_OK : E_OUTOFMEMORY;
}

class XorCoder final : public ICompressCoder, public ICompressSetCoderProperties, public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(ICompressCoder)
    Z7_COM_QI_ENTRY(ICompressSetCoderProperties)
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
  public:
    Z7_COM7F_IMF(SetCoderProperties(const PROPID*, const PROPVARIANT*, UInt32)) override {
        return S_OK;
    }
    Z7_COM7F_IMF(Code(ISequentialInStream* input, ISequentialOutStream* output, const UInt64*, const UInt64*,
                      ICompressProgressInfo* progress))
    override {
        std::array<Byte, 32768> buffer;
        UInt64 completed = 0;
        for (;;) {
            UInt32 read = 0;
            RINOK(input->Read(buffer.data(), buffer.size(), &read))
            if (!read)
                return S_OK;
            for (UInt32 i = 0; i < read; ++i)
                buffer[i] ^= 0xA5;
            UInt32 offset = 0;
            while (offset < read) {
                UInt32 written = 0;
                RINOK(output->Write(buffer.data() + offset, read - offset, &written))
                if (!written)
                    return E_FAIL;
                offset += written;
            }
            completed += read;
            if (progress) {
                RINOK(progress->SetRatioInfo(&completed, &completed))
            }
        }
    }
};

class NullOutput final : public ISequentialOutStream, public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(ISequentialOutStream)
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
  public:
    Z7_COM7F_IMF(Write(const void*, UInt32 size, UInt32* written)) override {
        if (written)
            *written = size;
        return S_OK;
    }
};

class SampleArchive final : public IInArchive, public IOutArchive, public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IInArchive)
    Z7_COM_QI_ENTRY(IOutArchive)
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
    CMyComPtr<IInStream> stream;
    UInt64 size = 0;

  public:
    Z7_COM7F_IMF(GetFileTimeType(UInt32* type)) override {
        *type = NFileTimeType::kWindows;
        return S_OK;
    }
    Z7_COM7F_IMF(UpdateItems(ISequentialOutStream* output, UInt32 count, IArchiveUpdateCallback* callback))
    override {
        if (count != 1 || !host)
            return E_INVALIDARG;
        Int32 newData = 0, newProperties = 0;
        UInt32 oldIndex = 0;
        RINOK(callback->GetUpdateItemInfo(0, &newData, &newProperties, &oldIndex))
        CMyComPtr<ISequentialInStream> input;
        if (newData) {
            RINOK(callback->GetStream(0, &input))
        } else if (oldIndex == 0 && stream) {
            RINOK(stream->Seek(8, STREAM_SEEK_SET, nullptr))
            input = stream;
        }
        if (!input)
            return E_INVALIDARG;
        UInt32 offset = 0;
        while (offset < 8) {
            UInt32 written = 0;
            RINOK(output->Write(signature + offset, 8 - offset, &written))
            if (!written)
                return E_FAIL;
            offset += written;
        }
        UInt32 methods = 0;
        RINOK(host->GetNumMethods(&methods))
        CMyComPtr<ICompressCoder> copy;
        for (UInt32 i = 0; i < methods; ++i) {
            PROPVARIANT property{};
            RINOK(host->GetProperty(i, NMethodPropID::kID, &property))
            if (property.vt == VT_UI8 && property.uhVal.QuadPart == 0) {
                RINOK(host->CreateEncoder(i, &IID_ICompressCoder, reinterpret_cast<void**>(&copy)))
                break;
            }
        }
        if (!copy)
            return E_FAIL;
        RINOK(copy->Code(input, output, nullptr, nullptr, nullptr))
        return callback->SetOperationResult(NArchive::NUpdate::NOperationResult::kOK);
    }
    Z7_COM7F_IMF(Open(IInStream* input, const UInt64*, IArchiveOpenCallback*)) override {
        char prefix[8];
        UInt32 read = 0;
        RINOK(input->Seek(0, STREAM_SEEK_SET, nullptr))
        RINOK(input->Read(prefix, sizeof(prefix), &read))
        if (read != sizeof(prefix) || std::memcmp(prefix, signature, sizeof(prefix)))
            return S_FALSE;
        UInt64 total = 0;
        RINOK(input->Seek(0, STREAM_SEEK_END, &total))
        size = total - sizeof(prefix);
        stream = input;
        return S_OK;
    }
    Z7_COM7F_IMF(Close()) override {
        stream.Release();
        return S_OK;
    }
    Z7_COM7F_IMF(GetNumberOfItems(UInt32* count)) override {
        *count = 1;
        return S_OK;
    }
    Z7_COM7F_IMF(GetProperty(UInt32 index, PROPID property, PROPVARIANT* value)) override {
        value->vt = VT_EMPTY;
        if (index)
            return E_INVALIDARG;
        switch (property) {
        case kpidPath:
            return textProperty(L"payload.txt", value);
        case kpidMethod:
            return textProperty(L"Copy (host)", value);
        case kpidSize:
        case kpidPackSize:
            value->vt = VT_UI8;
            value->uhVal.QuadPart = size;
            break;
        case kpidIsDir:
        case kpidEncrypted:
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_FALSE;
            break;
        }
        return S_OK;
    }
    Z7_COM7F_IMF(Extract(const UInt32* indices, UInt32 count, Int32 test, IArchiveExtractCallback* callback))
    override {
        if (!count)
            return S_OK;
        if (count != static_cast<UInt32>(-1) && (count != 1 || indices[0]))
            return E_INVALIDARG;
        if (!host)
            return E_FAIL;
        CMyComPtr<IArchiveRequestMemoryUseCallback> memory;
        if (callback->QueryInterface(IID_IArchiveRequestMemoryUseCallback,
                                     reinterpret_cast<void**>(&memory)) == S_OK) {
            UInt64 allowed = 32768;
            UInt32 answer = NRequestMemoryAnswerFlags::k_Allow;
            RINOK(memory->RequestMemoryUse(NRequestMemoryUseFlags::k_DefaultLimit_Exceeded,
                                           NArchive::NEventIndexType::kInArcIndex, 0, nullptr, 65536,
                                           &allowed, &answer))
            if ((answer & NRequestMemoryAnswerFlags::k_Allow) == 0)
                return E_ABORT;
        }
        const Int32 mode =
            test ? NArchive::NExtract::NAskMode::kTest : NArchive::NExtract::NAskMode::kExtract;
        RINOK(callback->SetTotal(size))
        CMyComPtr<ISequentialOutStream> output;
        RINOK(callback->GetStream(0, &output, mode))
        if (!output && !test)
            return S_OK;
        if (!output)
            output = new NullOutput;
        RINOK(callback->PrepareOperation(mode))
        UInt32 methods = 0;
        RINOK(host->GetNumMethods(&methods))
        CMyComPtr<ICompressCoder> copy;
        for (UInt32 i = 0; i < methods; ++i) {
            PROPVARIANT property{};
            RINOK(host->GetProperty(i, NMethodPropID::kID, &property))
            if (property.vt == VT_UI8 && property.uhVal.QuadPart == 0) {
                RINOK(host->CreateDecoder(i, &IID_ICompressCoder, reinterpret_cast<void**>(&copy)))
                break;
            }
        }
        if (!copy)
            return E_FAIL;
        RINOK(stream->Seek(8, STREAM_SEEK_SET, nullptr))
        RINOK(copy->Code(stream, output, &size, &size, nullptr))
        RINOK(callback->SetCompleted(&size))
        return callback->SetOperationResult(NArchive::NExtract::NOperationResult::kOK);
    }
    Z7_COM7F_IMF(GetArchiveProperty(PROPID property, PROPVARIANT* value)) override {
        value->vt = VT_EMPTY;
        if (property == kpidPhySize) {
            value->vt = VT_UI8;
            value->uhVal.QuadPart = size + 8;
        }
        return S_OK;
    }
    Z7_COM7F_IMF(GetNumberOfProperties(UInt32* count)) override {
        *count = 3;
        return S_OK;
    }
    Z7_COM7F_IMF(GetPropertyInfo(UInt32 index, BSTR* name, PROPID* property, VARTYPE* type)) override {
        *name = nullptr;
        const PROPID properties[]{kpidPath, kpidSize, kpidMethod};
        const VARTYPE types[]{VT_BSTR, VT_UI8, VT_BSTR};
        if (index >= 3)
            return E_INVALIDARG;
        *property = properties[index];
        *type = types[index];
        return S_OK;
    }
    Z7_COM7F_IMF(GetNumberOfArchiveProperties(UInt32* count)) override {
        *count = 1;
        return S_OK;
    }
    Z7_COM7F_IMF(GetArchivePropertyInfo(UInt32 index, BSTR* name, PROPID* property, VARTYPE* type)) override {
        if (index)
            return E_INVALIDARG;
        *name = nullptr;
        *property = kpidPhySize;
        *type = VT_UI8;
        return S_OK;
    }
};
} // namespace

MODULE_API GetModuleProp(PROPID property, PROPVARIANT* value) {
    value->vt = VT_UI4;
    if (property == NModulePropID::kInterfaceType)
        value->ulVal = NModuleInterfaceType::k_IUnknown_VirtDestructor_ThisModule;
    else if (property == NModulePropID::kVersion)
        value->ulVal = (26 << 16) | 3;
    else
        value->vt = VT_EMPTY;
    return S_OK;
}
MODULE_API SetCodecs(ICompressCodecsInfo* codecs) {
    host = codecs;
    return S_OK;
}
MODULE_API GetNumberOfMethods(UInt32* count) {
    *count = 1;
    return S_OK;
}
MODULE_API GetMethodProperty(UInt32 index, PROPID property, PROPVARIANT* value) {
    value->vt = VT_EMPTY;
    if (index)
        return E_INVALIDARG;
    switch (property) {
    case NMethodPropID::kID:
        value->vt = VT_UI8;
        value->uhVal.QuadPart = codecId;
        break;
    case NMethodPropID::kName:
        return textProperty(L"M7Xor", value);
    case NMethodPropID::kEncoder:
    case NMethodPropID::kDecoder:
        return bytesProperty(&codecClass, sizeof(codecClass), value);
    case NMethodPropID::kPackStreams:
    case NMethodPropID::kUnpackStreams:
        value->vt = VT_UI4;
        value->ulVal = 1;
        break;
    case NMethodPropID::kIsFilter:
        value->vt = VT_BOOL;
        value->boolVal = VARIANT_FALSE;
        break;
    }
    return S_OK;
}
MODULE_API CreateDecoder(UInt32 index, const GUID* iid, void** object) {
    *object = nullptr;
    if (index || *iid != IID_ICompressCoder)
        return E_NOINTERFACE;
    auto* coder = new XorCoder;
    static_cast<ICompressCoder*>(coder)->AddRef();
    *object = static_cast<ICompressCoder*>(coder);
    return S_OK;
}
MODULE_API CreateEncoder(UInt32 index, const GUID* iid, void** object) {
    return CreateDecoder(index, iid, object);
}
MODULE_API GetNumberOfFormats(UInt32* count) {
    *count = 1;
    return S_OK;
}
MODULE_API GetHandlerProperty2(UInt32 index, PROPID property, PROPVARIANT* value) {
    value->vt = VT_EMPTY;
    if (index)
        return E_INVALIDARG;
    switch (property) {
    case NArchive::NHandlerPropID::kName:
        return textProperty(L"M7Module", value);
    case NArchive::NHandlerPropID::kExtension:
        return textProperty(L"m7module", value);
    case NArchive::NHandlerPropID::kClassID:
        return bytesProperty(&archiveClass, sizeof(archiveClass), value);
    case NArchive::NHandlerPropID::kSignature:
        return bytesProperty(signature, 8, value);
    case NArchive::NHandlerPropID::kUpdate:
        value->vt = VT_BOOL;
        value->boolVal = VARIANT_TRUE;
        break;
    }
    return S_OK;
}
MODULE_API CreateObject(const GUID* clsid, const GUID* iid, void** object) {
    *object = nullptr;
    if (*clsid == archiveClass && (*iid == IID_IInArchive || *iid == IID_IOutArchive)) {
        auto* archive = new SampleArchive;
        static_cast<IInArchive*>(archive)->AddRef();
        if (*iid == IID_IInArchive)
            *object = static_cast<IInArchive*>(archive);
        else
            *object = static_cast<IOutArchive*>(archive);
        return S_OK;
    }
    return E_NOINTERFACE;
}
