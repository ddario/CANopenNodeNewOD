/*
 * CANopen Object Dictionary interface
 *
 * @file        CO_ODinterface.c
 * @author      Janez Paternoster
 * @copyright   2020 Janez Paternoster
 *
 * This file is part of CANopenNode, an opensource CANopen Stack.
 * Project home page is <https://github.com/CANopenNode/CANopenNode>.
 * For more information on CANopen see <http://www.can-cia.org/>.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#define OD_DEFINITION
#include "301/CO_ODinterface.h"


/******************************************************************************/
OD_size_t OD_readOriginal(OD_stream_t *stream, uint8_t subIndex,
                          void *buf, OD_size_t count, ODR_t *returnCode)
{
    (void) subIndex;

    if (stream == NULL || buf == NULL || returnCode == NULL) {
        if (returnCode != NULL) *returnCode = ODR_DEV_INCOMPAT;
        return 0;
    }

    OD_size_t dataLenToCopy = stream->dataLength; /* length of OD variable */
    uint8_t *odData = stream->data;

    if (odData == NULL) {
        *returnCode = ODR_SUB_NOT_EXIST;
        return 0;
    }

    *returnCode = ODR_OK;

    /* If previous read was partial or OD variable length is larger than
     * current buffer size, then data was (will be) read in several segments */
    if (stream->dataOffset > 0 || dataLenToCopy > count) {
        if (stream->dataOffset >= dataLenToCopy) {
            *returnCode = ODR_DEV_INCOMPAT;
            return 0;
        }
        /* Reduce for already copied data */
        dataLenToCopy -= stream->dataOffset;
        odData += stream->dataOffset;

        if (dataLenToCopy > count) {
            /* not enough space in destination buffer */
            dataLenToCopy = count;
            stream->dataOffset += dataLenToCopy;
            *returnCode = ODR_PARTIAL;
        }
        else {
            stream->dataOffset = 0; /* copy finished, reset offset */
        }
    }

    CO_LOCK_OD();
    memcpy(buf, odData, dataLenToCopy);
    CO_UNLOCK_OD();
    return dataLenToCopy;
}

/******************************************************************************/
OD_size_t OD_writeOriginal(OD_stream_t *stream, uint8_t subIndex,
                           const void *buf, OD_size_t count, ODR_t *returnCode)
{
    (void) subIndex;

    if (stream == NULL || buf == NULL || returnCode == NULL) {
        if (returnCode != NULL) *returnCode = ODR_DEV_INCOMPAT;
        return 0;
    }

    OD_size_t dataLenToCopy = stream->dataLength; /* length of OD variable */
    uint8_t *odData = stream->data;

    if (odData == NULL) {
        *returnCode = ODR_SUB_NOT_EXIST;
        return 0;
    }

    *returnCode = ODR_OK;

    /* If previous write was partial or OD variable length is larger than
     * current buffer size, then data was (will be) written in several
     * segments */
    if (stream->dataOffset > 0 || dataLenToCopy > count) {
        if (stream->dataOffset >= dataLenToCopy) {
            *returnCode = ODR_DEV_INCOMPAT;
            return 0;
        }
        /* reduce for already copied data */
        dataLenToCopy -= stream->dataOffset;
        odData += stream->dataOffset;

        if (dataLenToCopy > count) {
            /* Remaining data space in OD variable is larger than current count
             * of data, so only current count of data will be copied */
            dataLenToCopy = count;
            stream->dataOffset += dataLenToCopy;
            *returnCode = ODR_PARTIAL;
        }
        else {
            stream->dataOffset = 0; /* copy finished, reset offset */
        }
    }

    if (dataLenToCopy < count) {
        /* OD variable is smaller than current amount of data */
        *returnCode = ODR_DATA_LONG;
        return 0;
    }

    CO_LOCK_OD();
    memcpy(odData, buf, dataLenToCopy);
    CO_UNLOCK_OD();
    return dataLenToCopy;
}

/* Read value from variable from Object Dictionary disabled, see OD_IO_t*/
static OD_size_t OD_readDisabled(OD_stream_t *stream, uint8_t subIndex,
                                 void *buf, OD_size_t count,
                                 ODR_t *returnCode)
{
    (void) stream; (void) subIndex; (void) buf; (void) count;

    if (returnCode != NULL) *returnCode = ODR_UNSUPP_ACCESS;
    return 0;
}

/* Write value to variable from Object Dictionary disabled, see OD_IO_t */
static OD_size_t OD_writeDisabled(OD_stream_t *stream, uint8_t subIndex,
                                  const void *buf, OD_size_t count,
                                  ODR_t *returnCode)
{
    (void) stream; (void) subIndex; (void) buf; (void) count;

    if (returnCode != NULL) *returnCode = ODR_UNSUPP_ACCESS;
    return 0;
}


/******************************************************************************/
const OD_entry_t *OD_find(const OD_t *od, uint16_t index) {
    if (od == NULL || od->size == 0) {
        return NULL;
    }

    uint16_t cur;
    uint16_t min = 0;
    uint16_t max = od->size - 1;

    /* Fast search in ordered Object Dictionary. If indexes are mixed,
     * this won't work. If Object Dictionary has up to N entries, then the
     * max number of loop passes is log2(N) */
    while (min < max) {
        /* get entry between min and max */
        cur = (min + max) >> 1;
        const OD_entry_t* entry = &od->list[cur];

        if (index == entry->index) {
            return entry;
        }

        if (index < entry->index) {
            max = (cur > 0) ? (cur - 1) : cur;
        }
        else {
            min = cur + 1;
        }
    }

    if (min == max) {
        const OD_entry_t* entry = &od->list[min];
        if (index == entry->index) {
            return entry;
        }
    }

    return NULL;  /* entry does not exist in OD */
}

/******************************************************************************/
ODR_t OD_getSub(const OD_entry_t *entry, uint8_t subIndex,
                OD_subEntry_t *subEntry, OD_IO_t *io, bool_t odOrig)
{
    if (entry == NULL || entry->odObject == NULL) return ODR_IDX_NOT_EXIST;
    if (io == NULL) return ODR_DEV_INCOMPAT;

    OD_stream_t *stream = (OD_stream_t *)io;
    const OD_obj_var_t *odv = entry->odObject;
    const OD_obj_extended_t *odExt = odv->ext;
    uint8_t odBasicType = entry->odObjectType & ODT_TYPE_MASK;
    OD_attr_t attr = 0;

    /* Is object type extended? */
    if ((entry->odObjectType & ODT_EXTENSION_MASK) != 0 && odExt == NULL) {
          return ODR_DEV_INCOMPAT;
    }

    /* Attribute, dataObjectOriginal and dataLength, depends on object type */
    if (odBasicType == ODT_VAR) {
        if (subIndex > 0) return ODR_SUB_NOT_EXIST;

        attr = odv->attribute;
        stream->data = odv->data;
        stream->dataLength = odv->dataLength;
    }
    else if (odBasicType == ODT_ARR) {
        if (subIndex >= entry->subEntriesCount) return ODR_SUB_NOT_EXIST;

        if (subIndex == 0) {
            attr = odv->attribute;
            stream->data = odv->data;
            stream->dataLength = 1;
        }
        else {
            const OD_obj_array_t *oda = entry->odObject;

            attr = oda->attribute;

            /* Preset data to NULL */
            stream->data = NULL;
            stream->dataLength = oda->dataElementLength;

            if (oda->data != NULL) {
                /*
                 * FIXME: sub-indexes in arrays could be not consecutive
                 * as for records??
                 */
                size_t offset = oda->dataElementSizeof * (subIndex - 1);
                uint8_t *data = (uint8_t *) oda->data;
                stream->data = data + offset;
            }
        }
    }
    else if (odBasicType == ODT_REC) {
        uint8_t i;
        const OD_obj_record_t *odrArr = entry->odObject;
        const OD_obj_var_t *odr = NULL;

        for (i = 0; i < entry->subEntriesCount; i++) {
            if (odrArr[i].subIndex == subIndex) {
                odr = (const OD_obj_var_t *) &odrArr[i];
                break;
            }
        }

        if (odr == NULL) return ODR_SUB_NOT_EXIST;

        attr = odr->attribute;
        stream->data = odr->data;
        stream->dataLength = odr->dataLength;
    }
    else {
        return ODR_DEV_INCOMPAT;
    }

    /* read, write and dataObject, direct or with IO extension */
    if (odExt == NULL || odOrig) {
        io->read = OD_readOriginal;
        io->write = OD_writeOriginal;
        stream->object = NULL;
    }
    else {
        io->read  = odExt->read  != NULL ? odExt->read  : OD_readDisabled;
        io->write = odExt->write != NULL ? odExt->write : OD_writeDisabled;
        stream->object = odExt->object;
    }

    /* common properties */
    if (subEntry != NULL) {
        subEntry->index = entry->index;
        subEntry->subIndex = subIndex;
        subEntry->attribute = attr;
        subEntry->subEntriesCount = entry->subEntriesCount;
        subEntry->flagsPDO = odExt != NULL ? odExt->flagsPDO : NULL;
    }

    /* Initialize stream offset */
    stream->dataOffset = 0;

    return ODR_OK;
}

/******************************************************************************/
uint32_t OD_getSDOabCode(ODR_t returnCode) {
    static const uint32_t abortCodes[ODR_COUNT] = {
        0x00000000UL, /* No abort */
        0x05040005UL, /* Out of memory */
        0x06010000UL, /* Unsupported access to an object */
        0x06010001UL, /* Attempt to read a write only object */
        0x06010002UL, /* Attempt to write a read only object */
        0x06020000UL, /* Object does not exist in the object dictionary */
        0x06040041UL, /* Object cannot be mapped to the PDO */
        0x06040042UL, /* Num and len of object to be mapped exceeds PDO len */
        0x06040043UL, /* General parameter incompatibility reasons */
        0x06040047UL, /* General internal incompatibility in device */
        0x06060000UL, /* Access failed due to hardware error */
        0x06070010UL, /* Data type does not match, length does not match */
        0x06070012UL, /* Data type does not match, length too high */
        0x06070013UL, /* Data type does not match, length too short */
        0x06090011UL, /* Sub index does not exist */
        0x06090030UL, /* Invalid value for parameter (download only). */
        0x06090031UL, /* Value range of parameter written too high */
        0x06090032UL, /* Value range of parameter written too low */
        0x06090036UL, /* Maximum value is less than minimum value. */
        0x060A0023UL, /* Resource not available: SDO connection */
        0x08000000UL, /* General error */
        0x08000020UL, /* Data cannot be transferred or stored to application */
        0x08000021UL, /* Data cannot be transferred because of local control */
        0x08000022UL, /* Data cannot be tran. because of present device state */
        0x08000023UL, /* Object dict. not present or dynamic generation fails */
        0x08000024UL  /* No data available */
    };

    return (returnCode < 0 || returnCode >= ODR_COUNT) ?
        abortCodes[ODR_DEV_INCOMPAT] : abortCodes[returnCode];
}

/******************************************************************************/
ODR_t OD_extensionIO_init(const OD_entry_t *entry,
                          void *object,
                          OD_size_t (*read)(OD_stream_t *stream,
                                            uint8_t subIndex,
                                            void *buf,
                                            OD_size_t count,
                                            ODR_t *returnCode),
                          OD_size_t (*write)(OD_stream_t *stream,
                                             uint8_t subIndex,
                                             const void *buf,
                                             OD_size_t count,
                                             ODR_t *returnCode))
{
    if (entry == NULL) {
        return ODR_IDX_NOT_EXIST;
    }

    const OD_obj_var_t *odo = entry->odObject;
    OD_obj_extended_t *ode = odo->ext;

    if ((entry->odObjectType & ODT_EXTENSION_MASK) == 0 || ode == NULL) {
        return ODR_PAR_INCOMPAT;
    }

    ode->object = object;
    ode->read = read;
    ode->write = write;

    return ODR_OK;
}


/******************************************************************************/
/**
 * Get variable from Object Dictionary
 *
 * @param entry OD entry returned by @ref OD_find().
 * @param subIndex Sub-index of the variable from the OD object.
 * @param [out] val Value will be written here.
 * @param len Size of value to retrieve from OD.
 * @param odOrig If true, then potential IO extension on entry will be
 * ignored and data in the original OD location will be returned.
 *
 * @return Value from @ref ODR_t, "ODR_OK" in case of success. Error, if
 * variable does not exist in object dictionary or it does not have the correct
 * length or other reason.
 */
ODR_t OD_get_value(const OD_entry_t *entry, uint8_t subIndex,
                void *val, OD_size_t len, bool_t odOrig)
{
    ODR_t ret;
    OD_IO_t io;
    OD_stream_t *stream = (OD_stream_t *)&io;

    ret = OD_getSub(entry, subIndex, NULL, &io, odOrig);

    if (ret != ODR_OK) return ret;
    if (val == NULL || stream->data == NULL) return ODR_DEV_INCOMPAT;
    if (stream->dataLength != len) return ODR_TYPE_MISMATCH;

    io.read(stream, subIndex, val, len, &ret);

    return ret;
}

/******************************************************************************/
/**
 * Get pointer to memory which holds data variable from Object Dictionary
 *
 * Function always returns "data" pointer, which points to data
 * in the original OD location. Take care, if IO extension is enabled on OD
 * entry. Take also care that "data" could be not aligned to data type.
 *
 * @param entry OD entry returned by @ref OD_find().
 * @param subIndex Sub-index of the variable from the OD object.
 * @param [out] val Pointer to variable will be written here.
 * @param [out] len Variable length will be written here. (allow NULL)
 *
 * @return Value from @ref ODR_t, "ODR_OK" in case of success. Error, if
 * variable does not exist in object dictionary or other reason.
 */
ODR_t OD_getPtr (const OD_entry_t *entry, uint8_t subIndex, void **val,
                 OD_size_t *len)
{
    ODR_t ret;
    OD_IO_t io;
    OD_stream_t *stream = (OD_stream_t *)&io;

    ret = OD_getSub(entry, subIndex, NULL, &io, true);

    if (ret != ODR_OK) return ret;
    if (val == NULL || stream->data == NULL || stream->dataLength == 0)
      return ODR_DEV_INCOMPAT;

    *val = stream->data;

    if (len != NULL) *len = stream->dataLength;

    return ret;
}

/******************************************************************************/
/**
 * Set variable in Object Dictionary
 *
 * @param entry OD entry returned by @ref OD_find().
 * @param subIndex Sub-index of the variable from the OD object.
 * @param val Pointer to value to write.
 * @param len Size of value to write.
 * @param odOrig If true, then potential IO extension on entry will be
 * ignored and data in the original OD location will be written.
 *
 * @return Value from @ref ODR_t, "ODR_OK" in case of success. Error, if
 * variable does not exist in object dictionary or it does not have the correct
 * length or other reason.
 */
ODR_t OD_set_value(const OD_entry_t *entry, uint8_t subIndex, void *val,
                   OD_size_t len, bool_t odOrig)
{
    ODR_t ret;
    OD_IO_t io;
    OD_stream_t *stream = (OD_stream_t *)&io;

    ret = OD_getSub(entry, subIndex, NULL, &io, odOrig);

    if (ret != ODR_OK) return ret;
    if (stream->dataLength != len) return ODR_TYPE_MISMATCH;

    io.write(stream, subIndex, val, len, &ret);

    return ret;
}
