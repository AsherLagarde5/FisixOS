#include "usb_storage.h"
#include "xhci.h"

static uint32_t be32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3]; }
static void put_be32(uint8_t *p, uint32_t v){ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static void put_be16(uint8_t *p, uint16_t v){ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void copy_str(char *dst, const uint8_t *src, uint32_t n){
    uint32_t i; for(i=0;i<n;i++) dst[i]=(char)src[i]; dst[n]=0;
    /* Trim trailing spaces, as SCSI inquiry strings are space-padded. */
    while(n>0 && dst[n-1]==' '){ dst[n-1]=0; n--; }
}
const char *usb_storage_status_name(int ready){ return ready?"ready":"not-ready"; }

int usb_storage_detect(usb_storage_t *out){
    xhci_info_t info; int i;
    if(!out) return 0;
    for(i=0;i<(int)sizeof(*out);i++) ((uint8_t*)out)[i]=0;
    if(!xhci_detect(&info)) return 0;
    out->found=1; out->controller_present=1;
    out->device_present=0; out->transport_ready=0;
    return 1;
}

int usb_storage_exec_scsi(usb_storage_t *dev, const uint8_t *cdb, uint8_t cdb_len,
                          uint8_t *data, uint32_t data_len, int data_in){
    usb_cbw_t cbw; usb_csw_t csw; int r, i;
    if(!dev || !dev->transport_ready || !cdb || cdb_len==0 || cdb_len>16) {
        /* exec is also used during init, before transport_ready is set:
           allow it if bulk endpoints are configured. */
        if(!dev || !dev->device_present) return -1;
        if(!cdb || cdb_len==0 || cdb_len>16) return -1;
    }
    for(i=0;i<(int)sizeof(cbw);i++) ((uint8_t*)&cbw)[i]=0;
    dev->tag++;
    cbw.signature=0x43425355u; cbw.tag=dev->tag; cbw.data_length=data_len;
    cbw.flags=data_in?0x80u:0x00u; cbw.lun=0; cbw.cb_length=cdb_len;
    for(i=0;i<cdb_len;i++) cbw.cb[i]=cdb[i];
    r=xhci_bulk_transfer(dev->slot, dev->bulk_out_ep, (uint8_t*)&cbw, 31, 0);
    if(r!=31) return -1;
    if(data_len>0 && data){
        if(data_in) r=xhci_bulk_transfer(dev->slot, dev->bulk_in_ep, data, data_len, 1);
        else r=xhci_bulk_transfer(dev->slot, dev->bulk_out_ep, data, data_len, 0);
        if(r!=(int)data_len) return -1;
    }
    for(i=0;i<(int)sizeof(csw);i++) ((uint8_t*)&csw)[i]=0;
    r=xhci_bulk_transfer(dev->slot, dev->bulk_in_ep, (uint8_t*)&csw, 13, 1);
    if(r!=13) return -1;
    if(csw.signature!=0x53425355u || csw.tag!=dev->tag) return -1;
    dev->last_csw_status=csw.status;
    if(csw.status!=0) return -2;
    return 0;
}

int usb_storage_test_unit_ready(usb_storage_t *dev){
    uint8_t cdb[6]={0}; int i;
    if(!dev || !dev->device_present) return 0;
    cdb[0]=USB_SCSI_TEST_UNIT_READY;
    for(i=0;i<10;i++){ if(usb_storage_exec_scsi(dev,cdb,6,0,0,0)==0) return 1; }
    return 0;
}
int usb_storage_request_sense(usb_storage_t *dev){
    uint8_t cdb[6]={0}; uint8_t sense[18]; int i;
    if(!dev || !dev->device_present) return 0;
    cdb[0]=USB_SCSI_REQUEST_SENSE; cdb[4]=18;
    for(i=0;i<(int)sizeof(sense);i++) sense[i]=0;
    if(usb_storage_exec_scsi(dev,cdb,6,sense,18,1)!=0) return 0;
    dev->last_sense_key=sense[2]&0x0F; dev->last_asc=sense[12]; dev->last_ascq=sense[13];
    return 1;
}
int usb_storage_inquiry(usb_storage_t *dev){
    uint8_t cdb[6]={0}; uint8_t inq[36]; int i;
    if(!dev || !dev->device_present) return 0;
    cdb[0]=USB_SCSI_INQUIRY; cdb[4]=36;
    for(i=0;i<(int)sizeof(inq);i++) inq[i]=0;
    if(usb_storage_exec_scsi(dev,cdb,6,inq,36,1)!=0) return 0;
    dev->removable=(inq[1]&0x80)!=0;
    copy_str(dev->inquiry_vendor, inq+8, 8);
    copy_str(dev->inquiry_product, inq+16, 16);
    copy_str(dev->inquiry_revision, inq+32, 4);
    return 1;
}
int usb_storage_read_capacity(usb_storage_t *dev){
    uint8_t cdb[10]={0}; uint8_t cap[8]; int i;
    if(!dev || !dev->device_present) return 0;
    cdb[0]=USB_SCSI_READ_CAPACITY_10;
    for(i=0;i<(int)sizeof(cap);i++) cap[i]=0;
    if(usb_storage_exec_scsi(dev,cdb,10,cap,8,1)!=0) return 0;
    dev->block_count=(uint64_t)be32(cap)+1;
    dev->block_size=be32(cap+4);
    if(dev->block_size==0 || dev->block_size>4096) return 0;
    return 1;
}

int usb_storage_init(usb_storage_t *out){
    uint8_t slot=0, in_ep=0, out_ep=0, uasp=0; uint16_t vendor=0, product=0, in_mps=0, out_mps=0; int i;
    if(!out) return 0;
    for(i=0;i<(int)sizeof(*out);i++) ((uint8_t*)out)[i]=0;
    /* xhci_bulk_init() enumerates the first device, parses its config
       for a Mass Storage interface, and configures its bulk endpoints.
       It must run after xhci_init() (kernel specific-driver stage). */
    if(!xhci_bulk_init(&slot,&in_ep,&out_ep,&in_mps,&out_mps,&uasp,&vendor,&product)) return 0;
    out->found=1; out->controller_present=1; out->device_present=1; out->slot=slot;
    out->vendor_id=vendor; out->product_id=product;
    out->bulk_in_ep=in_ep; out->bulk_out_ep=out_ep; out->bulk_in_mps=in_mps; out->bulk_out_mps=out_mps;
    out->uasp_detected=uasp; out->bot_fallback=uasp?1:0;
    /* Transport is usable for CBW/CSW now; INQUIRY/CAPACITY prove it. */
    if(!usb_storage_test_unit_ready(out)) usb_storage_request_sense(out);
    if(!usb_storage_inquiry(out)) return 0;
    if(!usb_storage_read_capacity(out)) return 0;
    out->transport_ready=1;
    return 1;
}

int usb_storage_read_blocks(usb_storage_t *dev, uint32_t lba, uint32_t count, uint8_t *buf){
    uint8_t cdb[10]={0}; uint32_t bytes;
    if(!dev || !dev->transport_ready || !buf || count==0 || count>255) return 0;
    if(dev->block_size==0) return 0;
    if((uint64_t)lba+count>dev->block_count) return 0;
    bytes=count*dev->block_size;
    cdb[0]=USB_SCSI_READ_10; cdb[1]=0;
    put_be32(cdb+2, lba); put_be16(cdb+7, (uint16_t)count);
    /* READ (10) transfer length is in blocks, not bytes; exec data_len is bytes. */
    if(usb_storage_exec_scsi(dev,cdb,10,buf,bytes,1)!=0) return 0;
    return 1;
}
int usb_storage_write_blocks(usb_storage_t *dev, uint32_t lba, uint32_t count, const uint8_t *buf){
    uint8_t cdb[10]={0}; uint32_t bytes;
    /* BOUNDED, UNTESTED: no destructive write test was run in this task.
       Callers must use scratch media only. Boot never calls write. */
    if(!dev || !dev->transport_ready || !buf || count==0 || count>USB_STORAGE_MAX_WRITE_BLOCKS) return 0;
    if(dev->block_size==0) return 0;
    if((uint64_t)lba+count>dev->block_count) return 0;
    bytes=count*dev->block_size;
    cdb[0]=USB_SCSI_WRITE_10; cdb[1]=0;
    put_be32(cdb+2, lba); put_be16(cdb+7, (uint16_t)count);
    if(usb_storage_exec_scsi(dev,cdb,10,(uint8_t*)buf,bytes,0)!=0) return 0;
    return 1;
}
int usb_storage_read_bpb(usb_storage_t *dev, uint8_t *sector512){
    uint16_t bps; int i;
    if(!dev || !sector512) return 0;
    if(!usb_storage_read_blocks(dev,0,1,sector512)) return 0;
    /* BPB: jump at 0, bytes/sector at 11, boot signature 0x55AA at 510. */
    if(sector512[510]!=0x55 || sector512[511]!=0xAA) return 0;
    bps=(uint16_t)(sector512[11]|(sector512[12]<<8));
    if(bps!=512 && bps!=1024 && bps!=2048 && bps!=4096) return 0;
    (void)i;
    return 1;
}
int usb_storage_block_read(usb_storage_t *dev, uint32_t lba, uint8_t *buf){
    return usb_storage_read_blocks(dev,lba,1,buf);
}
uint32_t usb_storage_block_size(const usb_storage_t *dev){ return dev?dev->block_size:0; }
uint64_t usb_storage_block_count(const usb_storage_t *dev){ return dev?dev->block_count:0; }
