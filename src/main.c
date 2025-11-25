#include <tice.h>
#include <stdlib.h>
#include <string.h>
#include <usbdrvce.h>
#include <keypadc.h>
#include <ti/getcsc.h>
#include <ti/screen.h>

typedef struct {
    usb_device_t device;
    usb_endpoint_t in, out;
    usb_device_t host;
} global_t;

#define BUFFER_SIZE 1024
#define INPUT_BUFFER_SIZE 256
#define DISPLAY_WIDTH 26
#define DISPLAY_HEIGHT 8

global_t global;
char receiveBuffer[BUFFER_SIZE];
char displayBuffer[BUFFER_SIZE];
char inputBuffer[INPUT_BUFFER_SIZE];
int receiveIndex = 0;
int displayIndex = 0;
int inputIndex = 0;
bool connected = false;
bool waitingForResponse = false;
void* usbBuffer = NULL;

static usb_error_t handleBulkOut(usb_endpoint_t endpoint, usb_transfer_status_t status, 
                                  size_t transferred, usb_transfer_data_t* data);
static usb_error_t handleUsbEvent(usb_event_t event, void* event_data, void* callback_data);

void displayScreen(void);
void takeInput(void);
void sendMessage(void);
void appendToDisplay(const char* text);
void clearInput(void);

int main(void) {
    usb_error_t error;
    
    memset(&global, 0, sizeof(global_t));
    memset(receiveBuffer, 0, BUFFER_SIZE);
    memset(displayBuffer, 0, BUFFER_SIZE);
    memset(inputBuffer, 0, INPUT_BUFFER_SIZE);
    
    os_ClrHome();
    os_SetCursorPos(0, 0);
    os_PutStrFull("TI-84 ChatGPT");
    os_SetCursorPos(1, 0);
    os_PutStrFull("Initializing USB...");
    
    if ((error = usb_Init(handleUsbEvent, &global, NULL, USB_DEFAULT_INIT_FLAGS)) == USB_SUCCESS) {
        os_SetCursorPos(2, 0);
        os_PutStrFull("USB Ready!");
        os_SetCursorPos(3, 0);
        os_PutStrFull("Connect to browser");
        
        delay(1000);
        displayScreen();
        
        while (true) {
            kb_Scan();
            
            if (kb_Data[6] & kb_Clear) {
                break;
            }
            
            usb_WaitForInterrupt();
            
            if (connected && !waitingForResponse) {
                if (kb_Data[1] & kb_2nd) {
                    delay(200);
                    takeInput();
                }
            }
        }
    } else {
        os_SetCursorPos(2, 0);
        os_PutStrFull("USB Init Failed!");
        while (!os_GetCSC());
    }
    
    if (usbBuffer) {
        free(usbBuffer);
    }
    
    usb_Cleanup();
    return 0;
}

void displayScreen(void) {
    os_ClrHome();
    os_SetCursorPos(0, 0);
    os_PutStrFull("=== ChatGPT ===");
    
    os_SetCursorPos(1, 0);
    if (connected) {
        if (waitingForResponse) {
            os_PutStrFull("Status: Waiting...");
        } else {
            os_PutStrFull("Status: Ready");
        }
    } else {
        os_PutStrFull("Status: Disconnected");
    }
    
    os_SetCursorPos(2, 0);
    os_PutStrFull("--------------------------");
    
    if (displayIndex > 0) {
        os_SetCursorPos(3, 0);
        int row = 3;
        int col = 0;
        for (int i = 0; i < displayIndex && row < 9; i++) {
            if (displayBuffer[i] == '\n' || col >= DISPLAY_WIDTH) {
                row++;
                col = 0;
                if (displayBuffer[i] == '\n') continue;
            }
            if (row < 9) {
                os_SetCursorPos(row, col);
                char temp[2] = {displayBuffer[i], 0};
                os_PutStrFull(temp);
                col++;
            }
        }
    } else {
        os_SetCursorPos(4, 0);
        os_PutStrFull("Press 2nd to chat");
    }
    
    os_SetCursorPos(9, 0);
    os_PutStrFull("CLEAR=Quit");
}

void takeInput(void) {
    const char* chars = "\0\0\0\0\0\0\0\0\0\0\"WRMH\0\0?[VQLG\0\0:ZUPKFC\0 YTOJEB\0\0XSNIDA\0\0\0\0\0\0\0\0";
    
    clearInput();
    
    os_ClrHome();
    os_SetCursorPos(0, 0);
    os_PutStrFull("Enter message:");
    os_SetCursorPos(1, 0);
    os_PutStrFull("--------------------------");
    os_SetCursorPos(9, 0);
    os_PutStrFull("ENTER=Send CLEAR=Cancel");
    
    while (true) {
        uint8_t key = os_GetCSC();
        
        if (key == sk_Enter) {
            if (inputIndex > 0) {
                sendMessage();
            }
            break;
        } else if (key == sk_Clear) {
            break;
        } else if (key == sk_Del) {
            if (inputIndex > 0) {
                inputIndex--;
                inputBuffer[inputIndex] = 0;
                
                os_SetCursorPos(2, 0);
                for (int i = 0; i < 8; i++) {
                    os_SetCursorPos(2 + i, 0);
                    os_PutStrFull("                          ");
                }
                
                int row = 2;
                int col = 0;
                for (int i = 0; i < inputIndex && row < 9; i++) {
                    if (col >= DISPLAY_WIDTH) {
                        row++;
                        col = 0;
                    }
                    if (row < 9) {
                        os_SetCursorPos(row, col);
                        char temp[2] = {inputBuffer[i], 0};
                        os_PutStrFull(temp);
                        col++;
                    }
                }
            }
        } else if (chars[key] && inputIndex < INPUT_BUFFER_SIZE - 1) {
            inputBuffer[inputIndex++] = chars[key];
            inputBuffer[inputIndex] = 0;
            
            int row = 2;
            int col = 0;
            for (int i = 0; i < inputIndex && row < 9; i++) {
                if (col >= DISPLAY_WIDTH) {
                    row++;
                    col = 0;
                }
                if (row < 9) {
                    os_SetCursorPos(row, col);
                    char temp[2] = {inputBuffer[i], 0};
                    os_PutStrFull(temp);
                    col++;
                }
            }
        }
        
        delay(50);
    }
    
    displayScreen();
}

void sendMessage(void) {
    if (!connected || inputIndex == 0) return;
    
    appendToDisplay("You: ");
    appendToDisplay(inputBuffer);
    appendToDisplay("\n");
    
    usb_BulkTransfer(global.in, inputBuffer, inputIndex, 0, NULL);
    
    waitingForResponse = true;
    clearInput();
    displayScreen();
}

void appendToDisplay(const char* text) {
    int len = strlen(text);
    int remaining = BUFFER_SIZE - displayIndex - 1;
    
    if (len > remaining) {
        int shift = len - remaining + 100;
        if (shift < displayIndex) {
            memmove(displayBuffer, displayBuffer + shift, displayIndex - shift);
            displayIndex -= shift;
        } else {
            displayIndex = 0;
            memset(displayBuffer, 0, BUFFER_SIZE);
        }
    }
    
    strncpy(displayBuffer + displayIndex, text, len);
    displayIndex += len;
    displayBuffer[displayIndex] = 0;
}

void clearInput(void) {
    memset(inputBuffer, 0, INPUT_BUFFER_SIZE);
    inputIndex = 0;
}

static usb_error_t handleBulkOut(usb_endpoint_t endpoint, usb_transfer_status_t status,
                                  size_t transferred, usb_transfer_data_t* data) {
    if (status == USB_TRANSFER_COMPLETED && transferred > 0) {
        if (receiveIndex + transferred < BUFFER_SIZE) {
            memcpy(receiveBuffer + receiveIndex, data, transferred);
            receiveIndex += transferred;
            receiveBuffer[receiveIndex] = 0;
            
            if (strchr(receiveBuffer, '\n')) {
                char* newline = strchr(receiveBuffer, '\n');
                *newline = 0;
                
                appendToDisplay("AI: ");
                appendToDisplay(receiveBuffer);
                appendToDisplay("\n");
                
                receiveIndex = 0;
                memset(receiveBuffer, 0, BUFFER_SIZE);
                
                waitingForResponse = false;
                displayScreen();
            }
        }
        
        return usb_ScheduleBulkTransfer(endpoint, data, 64, handleBulkOut, data);
    }
    
    return USB_SUCCESS;
}

static usb_error_t handleUsbEvent(usb_event_t event, void* event_data, void* callback_data) {
    global_t* global_ptr = (global_t*)callback_data;
    usb_error_t error = USB_SUCCESS;
    
    switch ((unsigned)event) {
        case USB_HOST_CONFIGURE_EVENT: {
            global_ptr->host = usb_FindDevice(NULL, NULL, USB_SKIP_HUBS);
            if (!global_ptr->host) {
                error = USB_ERROR_NO_DEVICE;
                break;
            }
            
            global_ptr->out = usb_GetDeviceEndpoint(global_ptr->host, 0x02);
            global_ptr->in = usb_GetDeviceEndpoint(global_ptr->host, 0x81);
            
            if (!global_ptr->in || !global_ptr->out) {
                error = USB_ERROR_SYSTEM;
                break;
            }
            
            usb_SetEndpointFlags(global_ptr->in, USB_AUTO_TERMINATE);
            
            if (!usbBuffer) {
                usbBuffer = malloc(64);
                if (!usbBuffer) {
                    error = USB_ERROR_NO_MEMORY;
                    break;
                }
            }
            
            connected = true;
            handleBulkOut(global_ptr->out, USB_TRANSFER_COMPLETED, 0, usbBuffer);
            displayScreen();
            break;
        }
        
        case USB_DEVICE_DISCONNECTED_EVENT:
            if (global_ptr->device == event_data) {
                global_ptr->device = NULL;
                global_ptr->in = global_ptr->out = NULL;
                connected = false;
                displayScreen();
            }
            break;
            
        case USB_DEVICE_CONNECTED_EVENT:
            if (!(usb_GetRole() & USB_ROLE_DEVICE)) {
                error = usb_ResetDevice((usb_device_t)event_data);
            }
            break;
            
        case USB_DEVICE_ENABLED_EVENT:
            global_ptr->device = (usb_device_t)event_data;
            break;
            
        default:
            break;
    }
    
    return error;
}
