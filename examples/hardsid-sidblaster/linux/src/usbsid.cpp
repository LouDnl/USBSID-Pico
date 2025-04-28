#include "usbsid.hpp"

#pragma GCC diagnostic ignored "-Wnarrowing"

unsigned char result[LEN_IN_BUFFER];
int out_buffer_length;


int usbSIDSetup(void)
{
    printf("[USBSID] Starting setup\r\n");
    rc = read_completed = write_completed = -1;
    out_buffer_length = (ASYNC_THREADING == 0) ? LEN_OUT_BUFFER : LEN_OUT_BUFFER/* LEN_OUT_BUFFER_ASYNC */;

    /* - set line encoding: here 9600 8N1
     * 9000000 = 0x895440 -> 0x40, 0x54, 0x89 in little endian
     */
    unsigned char encoding[] = { 0x40, 0x54, 0x89, 0x00, 0x00, 0x00, 0x08 };

     /* Initialize libusb */
    rc = libusb_init(NULL);
    // rc = libusb_init_context(&ctx, /*options=NULL, /*num_options=*/0);  // NOTE: REQUIRES LIBUSB 1.0.27!!
    if (rc < 0) {
        fprintf(stderr, "Error initializing libusb: %d %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
        goto out;
    }
    printf("[USBSID] libusb_init complete\r\n");

    /* Set debugging output to min/max (4) level */
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 0);

    /* Look for a specific device and open it. */
    devh = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!devh) {
        fprintf(stderr, "Error opening USB device with VID & PID: %d %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
        rc = -1;
        goto out;
    }
    printf("[USBSID] libusb_open_device_with_vid_pid complete\r\n");

    /* As we are dealing with a CDC-ACM device, it's highly probable that
     * Linux already attached the cdc-acm driver to this device.
     * We need to detach the drivers from all the USB interfaces. The CDC-ACM
     * Class defines two interfaces: the Control interface and the
     * Data interface.
     */
    for (int if_num = 0; if_num < 2; if_num++) {
        if (libusb_kernel_driver_active(devh, if_num)) {
            libusb_detach_kernel_driver(devh, if_num);
        }
        rc = libusb_claim_interface(devh, if_num);
        if (rc < 0) {
            fprintf(stderr, "Error claiming interface: %d, %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
            rc = -1;
            goto out;
        }
    }
    printf("[USBSID] libusb_claim_interface complete\r\n");

    /* Start configuring the device:
     * - set line state
     */
    rc = libusb_control_transfer(devh, 0x21, 0x22, ACM_CTRL_DTR | ACM_CTRL_RTS, 0, NULL, 0, 0);
    if (rc < 0) {
        fprintf(stderr, "Error configuring line state during control transfer: %d, %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
        rc = -1;
        goto out;
    }
    printf("[USBSID] DTR RTS complete\r\n");

    rc = libusb_control_transfer(devh, 0x21, 0x20, 0, 0, encoding, sizeof(encoding), 0);
    if (rc < 0) {
        fprintf(stderr, "Error configuring line encoding during control transfer: %d, %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
        rc = -1;
        goto out;
    }
    printf("[USBSID] set encoding complete\r\n");

    if (ASYNC_THREADING == 0) {
        out_buffer = libusb_dev_mem_alloc(devh, LEN_OUT_BUFFER);
        if (out_buffer == NULL) {
            fprintf(stderr, "libusb_dev_mem_alloc failed on out_buffer, allocating with malloc\n");
            out_buffer = (uint8_t*)malloc( (sizeof(uint8_t)) * 3 );
        }
        printf("[USBSID] alloc out_buffer complete\r\n");
        transfer_out = libusb_alloc_transfer(0);
        printf("[USBSID] alloc transfer_out complete\r\n");
        libusb_fill_bulk_transfer(transfer_out, devh, ep_out_addr, out_buffer, LEN_OUT_BUFFER, sid_out, &write_completed, 0);
        printf("[USBSID] libusb_fill_bulk_transfer transfer_out complete\r\n");

        in_buffer = libusb_dev_mem_alloc(devh, LEN_IN_BUFFER);
        if (in_buffer == NULL) {
            fprintf(stderr, "libusb_dev_mem_alloc failed on in_buffer, allocating with malloc\n");
            in_buffer = (uint8_t*)malloc( (sizeof(uint8_t)) * 1 );
        }
        printf("[USBSID] alloc in_buffer complete\r\n");
        transfer_in = libusb_alloc_transfer(0);
        printf("[USBSID] alloc transfer_in complete\r\n");
        libusb_fill_bulk_transfer(transfer_in, devh, ep_in_addr, in_buffer, LEN_IN_BUFFER, sid_in, &read_completed, 0);
        printf("[USBSID] libusb_fill_bulk_transfer transfer_in complete\r\n");
    } else {
        out_buffer = libusb_dev_mem_alloc(devh, LEN_OUT_BUFFER /* LEN_OUT_BUFFER_ASYNC */);
        printf("[USBSID] alloc out_buffer complete\r\n");
        transfer_out = libusb_alloc_transfer(0);
        printf("[USBSID] alloc transfer_out complete\r\n");
        libusb_fill_bulk_transfer(transfer_out, devh, ep_out_addr, out_buffer, LEN_OUT_BUFFER /* LEN_OUT_BUFFER_ASYNC */, sid_out, &write_completed, 0);
        printf("[USBSID] libusb_fill_bulk_transfer transfer_out complete\r\n");
        rc = libusb_submit_transfer(transfer_out);
        if (rc < 0) {
            fprintf(stderr, "Error during transfer_out submit transfer: %s\n", libusb_error_name(rc));
            rc = -1;
            goto out;
        }
        printf("[USBSID] libusb_submit_transfer transfer_out complete\r\n");
    }

    fprintf(stdout, "[USBSID] opened\r\n");

    if (ASYNC_THREADING == 1) {
        pthread_create(&ptid, NULL, &usbSIDStart, NULL);
        fprintf(stdout, "[USBSID] pthread_create complete\r\n");
    }

    return rc;
out:
    usbSIDExit();
    return rc;
}

int usbSIDExit(void)
{
    printf("[USBSID] Closing\r\n");
    if (rc < 0) usbSIDPause();
    fprintf(stdout, "[USBSID] usbSIDPause complete\r\n");

    if (ASYNC_THREADING == 1) {
        exit_thread = 1;
        pthread_join(ptid, NULL);
        fprintf(stdout, "[USBSID] Thread joined\r\n");

        fprintf(stdout, "[USBSID] Thread exited\r\n");
    }

    rc = libusb_cancel_transfer(transfer_out);
    if (rc < 0 && rc != -5)
        fprintf(stderr, "Failed to cancel transfer %d - %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
    fprintf(stdout, "[USBSID] libusb_cancel_transfer transfer_out complete\r\n");

    rc = libusb_cancel_transfer(transfer_in);
    if (rc < 0 && rc != -5)
        fprintf(stderr, "Failed to cancel transfer %d - %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
    fprintf(stdout, "[USBSID] libusb_cancel_transfer transfer_in complete\r\n");

    rc = libusb_dev_mem_free(devh, in_buffer, LEN_IN_BUFFER);
    if (rc < 0)
        fprintf(stderr, "Failed to free in_buffer DMA memory: %d, %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
    fprintf(stdout, "[USBSID] libusb_dev_mem_free in_buffer complete\r\n");

    rc = libusb_dev_mem_free(devh, out_buffer, out_buffer_length);
    if (rc < 0)
        fprintf(stderr, "Failed to free out_buffer DMA memory: %d, %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
    fprintf(stdout, "[USBSID] libusb_dev_mem_free out_buffer complete\r\n");

    for (int if_num = 0; if_num < 2; if_num++) {
        if (libusb_kernel_driver_active(devh, if_num)) {
            rc = libusb_detach_kernel_driver(devh, if_num);
            fprintf(stderr, "libusb_detach_kernel_driver error: %d, %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
        }
        libusb_release_interface(devh, if_num);
        fprintf(stdout, "[USBSID] libusb_release_interface if_num %d complete\r\n", if_num);
    }

    if (devh) {
        libusb_close(devh);
        fprintf(stdout, "[USBSID] libusb_close complete\r\n");
    }

    libusb_exit(ctx);
    fprintf(stdout, "[USBSID] libusb_exit complete\r\n");

    devh = NULL;
    fprintf(stdout, "[USBSID] linux driver closed\r\n");
    return 0;
}

void *usbSIDStart(void*)
{
    fprintf(stdout, "[USBSID] Thread started\r\n");
    pthread_detach(pthread_self());
    fprintf(stdout, "[USBSID] Thread detached\r\n");

    while(!exit_thread) {
        while (!write_completed)
            libusb_handle_events_completed(ctx, &write_completed);
    }
    fprintf(stdout, "[USBSID] Thread finished\r\n");
    pthread_exit(NULL);
    return NULL;
}

void usbSIDWrite(unsigned char *buff)
{
    write_completed = 0;
    memcpy(out_buffer, buff, 3);
    if (ASYNC_THREADING == 0) {
        libusb_submit_transfer(transfer_out);
        libusb_handle_events_completed(ctx, NULL);
    }
}

void usbSIDRead_toBuff(unsigned char *writebuff)
{
    if (ASYNC_THREADING == 0) {  /* Reading not supported with async write */
        read_completed = 0;
        memcpy(out_buffer, writebuff, 4);
        libusb_submit_transfer(transfer_out);
        libusb_handle_events_completed(ctx, NULL);
        libusb_submit_transfer(transfer_in);
        libusb_handle_events_completed(ctx, &read_completed);
    }
}

unsigned char usbSIDRead(unsigned char *writebuff, unsigned char *buff)
{
    if (ASYNC_THREADING == 0) {  /* Reading not supported with async write */
        usbSIDRead_toBuff(writebuff);
        memcpy(buff, in_buffer, 1);
    } else {
        buff[0] = 0xFF;
    }

    return buff[0];
}

void usbSIDPause(void)
{
    unsigned char buff[4] = {0x2, 0x0, 0x0, 0x0};
    usbSIDWrite(buff);
}

void usbSIDReset(void)
{
    unsigned char buff[4] = {0x3, 0x0, 0x0, 0x0};
    usbSIDWrite(buff);
}

void LIBUSB_CALL sid_out(struct libusb_transfer *transfer)
{

    write_completed = (*(int *)transfer->user_data);


    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        rc = transfer->status;
        if (rc != LIBUSB_TRANSFER_CANCELLED) {
        fprintf(stderr, "Warning: transfer out interrupted with status %d, %s: %s\n", rc, libusb_error_name(rc), libusb_strerror(rc));
            print_libusb_transfer(transfer);
        }
        libusb_free_transfer(transfer);
        return;
    }

    if (transfer->actual_length != out_buffer_length) {
        fprintf(stderr, "Sent data length %d is different from the defined buffer length: %d or actual length %d\n", transfer->length, out_buffer_length, transfer->actual_length);
        print_libusb_transfer(transfer);
    }

    #ifdef USBSID_DEBUG
    {
        USBSIDDBG("SID_OUT: ");
        for (int i = 0; i <= sizeof(transfer->buffer); i++) USBSIDDBG("$%02x ", transfer->buffer[i]);
        USBSIDDBG("\r\n");
    }
    #endif

    if (ASYNC_THREADING == 1) {
        rc = libusb_submit_transfer(transfer);
        if (rc < 0) {
            fprintf(stderr, "Error during transfer_out submit transfer: %s\n", libusb_error_name(rc));
        }
    }

    write_completed = 1;
}

void LIBUSB_CALL sid_in(struct libusb_transfer *transfer)
{

    read_completed = (*(int *)transfer->user_data);

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        rc = transfer->status;
        if (rc != LIBUSB_TRANSFER_CANCELLED) {
            fprintf(stderr, "Warning: transfer in interrupted with status '%s'\n", libusb_error_name(rc));
            print_libusb_transfer(transfer);
        }
        libusb_free_transfer(transfer);
        return;
    }

    /* if (transfer->actual_length != LEN_IN_BUFFER) {
        fprintf(stderr, "Received data length %d is different from the defined buffer length: %d or actual length: %d\n", transfer->length, LEN_IN_BUFFER, transfer->actual_length);
        print_libusb_transfer(transfer_in);
    } */

    read_completed = 1;
    memcpy(result, in_buffer, 1); /* Copy read data to result */
    #ifdef USBSID_DEBUG
    {
        USBSIDDBG("SID_IN: ");
        for (int i = 0; i <= sizeof(transfer->buffer); i++) USBSIDDBG("$%02x ", transfer->buffer[i]);
        USBSIDDBG("\r\n");
    }
    #endif
}

void print_libusb_transfer(struct libusb_transfer *p_t)
{  int i;
  if ( NULL == p_t) {
    printf("libusb_transfer is empty\n");
  }
  else {
    printf("\r\n");
        printf("libusb_transfer structure:\n");
    printf("flags         =%x \n", p_t->flags);
    printf("endpoint      =%x \n", p_t->endpoint);
    printf("type          =%x \n", p_t->type);
    printf("timeout       =%d \n", p_t->timeout);
    printf("status        =%d '%s': '%s'\n", p_t->status, libusb_error_name(p_t->status), libusb_strerror(p_t->status));
    // length, and buffer are commands sent to the device
    printf("length        =%d \n", p_t->length);
    printf("actual_length =%d \n", p_t->actual_length);
    printf("user_data     =%p \n", p_t->user_data);
    printf("buffer        =%p \n", p_t->buffer);
    printf("buffer        =%x \n", *p_t->buffer);

    for (i=0; i < p_t->length; i++){
      printf("[%d: %x]", i, p_t->buffer[i]);
    }
        printf("\r\n");
  }
  return;
}

uint16_t sid_address(uint16_t addr)
{
  /* Set address for SID no# */
  /* D500, DE00 or DF00 is the second sid in SIDTYPE1, 3 & 4 */
  /* D500, DE00 or DF00 is the third sid in all other SIDTYPE */
  switch (addr) {
    case 0xD400 ... 0xD499:
      switch (SIDTYPE) {
        case SIDTYPE1:
        case SIDTYPE3:
        case SIDTYPE4:
          return addr; /* $D400 -> $D479 */
          break;
        case SIDTYPE2:
          return ((addr & SIDLMASK) >= 0x20) ? (SID2ADDR | (addr & SID1MASK)) : addr;
          break;
        case SIDTYPE5:
          return ((addr & SIDLMASK) >= 0x20) && ((addr & SIDLMASK) <= 0x39) /* $D420~$D439 -> $D440~$D459 */
                  ? (addr + 0x20)
                  : ((addr & SIDLMASK) >= 0x40) && ((addr & SIDLMASK) <= 0x59) /* $D440~$D459 -> $D420~$D439 */
                  ? (addr - 0x20)
                  : addr;
          break;
      }
      break;
    case 0xD500 ... 0xD599:
    case 0xDE00 ... 0xDF99:
      switch (SIDTYPE) {
        case SIDTYPE1:
        case SIDTYPE2:
          return (SID2ADDR | (addr & SID1MASK));
          break;
        case SIDTYPE3:
          return (SID3ADDR | (addr & SID1MASK));
          break;
        case SIDTYPE4:
          return (SID3ADDR | (addr & SID2MASK));
          break;
        case SIDTYPE5:
          return (SID1ADDR | (addr & SIDUMASK));
          break;
      }
      break;
    default:
      return addr;
      break;
  }

}

int64_t CycleFromTimestamp(timestamp_t timestamp)
{
  auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp - m_StartTime);
  return (int64_t)(nsec.count() * m_InvCPUcycleDurationNanoSeconds);
}

int WaitForCycle(int cycle)
{
    // HDBG("[cycles]%d\r\n", cycle);
    timestamp_t now = std::chrono::high_resolution_clock::now();
    double dur_ = cycle * m_CPUcycleDuration;
    duration_t dur = (duration_t)(int64_t)dur_;
    auto target_time = m_NextTime + dur;
    auto target_delta = target_time - now;
    auto wait_msec = std::chrono::duration_cast<std::chrono::milliseconds>(target_delta);
    // auto wait_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(target_delta);
    auto wait_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(target_delta * 1000);
    // HDBG("[now]%d [dur_]%d [dur]%d [target_time]%d [target_delta]%d [wait_msec]%d [wait_nsec]%d\r\n", now, dur_, dur, target_time, target_delta, wait_msec, wait_nsec);
    if (wait_msec.count() > 0) {
        /* for (int i = 0; i < (int)m_Devices.size(); ++i) {
        m_Devices[i]->SoftFlush();
        } */
        // POOP
        // HDBG("Waiting for %lu milliseconds\r\n", wait_msec);
        std::this_thread::sleep_for(wait_msec);
    }
    while (now < target_time) {
        now = std::chrono::high_resolution_clock::now();
    }
    m_CurrentTime       = now;
    m_NextTime          = target_time;
    int waited_cycles   = (int)(wait_nsec.count() * m_InvCPUcycleDurationNanoSeconds);
    // HDBG("wait_nsec: %d\r\n", wait_nsec);
    // HDBG("m_InvCPUcycleDurationNanoSeconds: %f\r\n", (double)m_InvCPUcycleDurationNanoSeconds);
    // HDBG("[waited_cycles]%d\r\n", waited_cycles);
    return waited_cycles;
}

void usbsid_reset(void)
{
    usbSIDReset();
}
int usbsid_open(void)
{
    return usbSIDSetup();
}
int usbsid_read(uint16_t addr, int chipno)
{
    unsigned char buff[3] = { 0x1, (addr + (0x20 * chipno)), 0x0 };   /* 3 Byte buffer */
    usbSIDRead(buff, result);
    return result[0];
}
void usbsid_store(uint16_t addr, uint8_t val, int chipno)
{
    unsigned char buff[3] = { 0x0, ((addr & 0x1f) + (0x20 * chipno)), val };   /* 3 Byte buffer */
    usbSIDWrite(buff);
}
