#include <tice.h>
#include <graphx.h>
#include <stdlib.h>
#include <string.h>
#include <usbdrvce.h>
#include <keypadc.h>
#include <ti/getcsc.h>

typedef struct {
    usb_device_t device;
    usb_endpoint_t in, out;
    usb_device_t host;
} global_t;

typedef struct Message {
    char* text;
    bool isUser;
    struct Message* next;
} Message;

#define BUFFER_SIZE 1024
#define INPUT_BUFFER_SIZE 256
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define MAX_LINE_WIDTH 38
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 8

global_t global;
char receiveBuffer[BUFFER_SIZE];
char inputBuffer[INPUT_BUFFER_SIZE];
int receiveIndex = 0;
int inputIndex = 0;
bool connected = false;
bool waitingForResponse = false;
bool inInputMode = false;
void* usbBuffer = NULL;

Message* messageHead = NULL;
Message* messageTail = NULL;
int scrollOffset = 0;
int totalMessagesHeight = 0;

static usb_error_t handleBulkOut(usb_endpoint_t endpoint, usb_transfer_status_t status, 
                                  size_t transferred, usb_transfer_data_t* data);
static usb_error_t handleUsbEvent(usb_event_t event, void* event_data, void* callback_data);

void addMessage(const char* text, bool isUser);
void drawMessages(void);
void drawInputScreen(void);
void drawConnectingScreen(void);
void takeInput(void);
void sendMessage(void);
void clearInput(void);
void freeMessages(void);

int main(void) {
    usb_error_t error;
    
    memset(&global, 0, sizeof(global_t));
    memset(receiveBuffer, 0, BUFFER_SIZE);
    memset(inputBuffer, 0, INPUT_BUFFER_SIZE);
    
    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_SetTextFGColor(255);
    gfx_SetTextBGColor(0);
    gfx_SetMonospaceFont(8);
    
    drawConnectingScreen();
    gfx_SwapDraw();
    
    if ((error = usb_Init(handleUsbEvent, &global, NULL, USB_DEFAULT_INIT_FLAGS)) == USB_SUCCESS) {
        while (true) {
            kb_Scan();
            
            if (kb_Data[6] & kb_Clear) {
                break;
            }
            
            usb_WaitForInterrupt();
            
            if (connected && !inInputMode) {
                if (kb_Data[1] & kb_2nd) {
                    delay(200);
                    takeInput();
                }
                
                // Scroll with arrow keys
                if (kb_Data[7] & kb_Up) {
                    if (scrollOffset > 0) {
                        scrollOffset -= 16;
                        if (scrollOffset < 0) scrollOffset = 0;
                        drawMessages();
                        gfx_SwapDraw();
                    }
                    delay(100);
                }
                if (kb_Data[7] & kb_Down) {
                    int maxScroll = totalMessagesHeight - 190;
                    if (scrollOffset < maxScroll && maxScroll > 0) {
                        scrollOffset += 16;
                        if (scrollOffset > maxScroll) scrollOffset = maxScroll;
                        drawMessages();
                        gfx_SwapDraw();
                    }
                    delay(100);
                }
            }
        }
    }
    
    if (usbBuffer) {
        free(usbBuffer);
    }
    
    freeMessages();
    usb_Cleanup();
    gfx_End();
    
    return 0;
}

void drawConnectingScreen(void) {
    gfx_FillScreen(0);
    gfx_SetColor(255);
    
    // Title
    gfx_PrintStringXY("TI-84 PLUS CE - CHATGPT", 60, 20);
    gfx_Line(20, 35, 300, 35);
    
    // Status
    if (connected) {
        gfx_PrintStringXY("Status: Connected!", 80, 60);
    } else {
        gfx_PrintStringXY("Status: Connecting...", 70, 60);
        gfx_PrintStringXY("Plug in USB and connect", 60, 80);
        gfx_PrintStringXY("from browser", 100, 95);
    }
    
    // Credits box
    gfx_Rectangle(70, 140, 180, 40);
    gfx_PrintStringXY("Made by y4shg", 105, 155);
    
    // Instructions
    gfx_PrintStringXY("Press CLEAR to quit", 85, 210);
}

void drawMessages(void) {
    gfx_FillScreen(0);
    gfx_SetColor(255);
    
    // Header
    if (waitingForResponse) {
        gfx_PrintStringXY("ChatGPT - Waiting...", 5, 5);
    } else {
        gfx_PrintStringXY("ChatGPT - Ready", 5, 5);
    }
    gfx_Line(0, 18, 320, 18);
    
    // Footer
    gfx_Line(0, 220, 320, 220);
    gfx_PrintStringXY("2nd:Type  Arrows:Scroll  CLR:Quit", 5, 225);
    
    // Messages area (y: 22 to 215)
    int yPos = 25 - scrollOffset;
    Message* msg = messageHead;
    totalMessagesHeight = 25;
    
    while (msg != NULL) {
        int textLen = strlen(msg->text);
        int linesNeeded = (textLen / MAX_LINE_WIDTH) + 1;
        if (linesNeeded > 20) linesNeeded = 20;
        
        int msgHeight = linesNeeded * (CHAR_HEIGHT + 2) + 8;
        
        // Only draw if visible
        if (yPos + msgHeight > 22 && yPos < 215) {
            int xStart;
            int boxWidth = 240;
            uint8_t boxColor;
            
            if (msg->isUser) {
                xStart = SCREEN_WIDTH - boxWidth - 10;
                boxColor = 29;  // Blue
            } else {
                xStart = 10;
                boxColor = 224; // Green
            }
            
            // Draw message box
            gfx_SetColor(boxColor);
            gfx_FillRectangle(xStart, yPos, boxWidth, msgHeight);
            gfx_SetColor(255);
            gfx_Rectangle(xStart, yPos, boxWidth, msgHeight);
            
            // Draw text line by line
            gfx_SetTextFGColor(0);
            int lineY = yPos + 4;
            int charsPrinted = 0;
            
            for (int line = 0; line < linesNeeded && charsPrinted < textLen; line++) {
                char lineBuf[MAX_LINE_WIDTH + 1];
                int charsThisLine = textLen - charsPrinted;
                if (charsThisLine > MAX_LINE_WIDTH) {
                    charsThisLine = MAX_LINE_WIDTH;
                    
                    // Try to break at space
                    for (int i = MAX_LINE_WIDTH - 1; i > MAX_LINE_WIDTH - 10 && i > 0; i--) {
                        if (msg->text[charsPrinted + i] == ' ') {
                            charsThisLine = i + 1;
                            break;
                        }
                    }
                }
                
                strncpy(lineBuf, msg->text + charsPrinted, charsThisLine);
                lineBuf[charsThisLine] = '\0';
                
                gfx_PrintStringXY(lineBuf, xStart + 4, lineY);
                
                charsPrinted += charsThisLine;
                lineY += CHAR_HEIGHT + 2;
            }
            
            gfx_SetTextFGColor(255);
        }
        
        yPos += msgHeight + 6;
        totalMessagesHeight = yPos + scrollOffset - 25;
        msg = msg->next;
    }
}

void drawInputScreen(void) {
    gfx_FillScreen(0);
    gfx_SetColor(255);
    
    // Header
    gfx_PrintStringXY("Type Your Message", 80, 5);
    gfx_Line(0, 18, 320, 18);
    
    // Input area box
    gfx_Rectangle(10, 25, 300, 180);
    
    // Draw input text
    gfx_SetTextFGColor(255);
    int yPos = 30;
    int xPos = 15;
    
    for (int i = 0; i < inputIndex; i++) {
        char c[2] = {inputBuffer[i], '\0'};
        
        if (xPos > 300) {
            xPos = 15;
            yPos += CHAR_HEIGHT + 2;
        }
        
        if (yPos > 195) break; // Don't draw past box
        
        gfx_PrintStringXY(c, xPos, yPos);
        xPos += CHAR_WIDTH;
    }
    
    // Draw cursor
    if (xPos > 300) {
        xPos = 15;
        yPos += CHAR_HEIGHT + 2;
    }
    if (yPos <= 195) {
        gfx_FillRectangle(xPos, yPos, 6, CHAR_HEIGHT);
    }
    
    // Footer
    gfx_Line(0, 210, 320, 210);
    gfx_PrintStringXY("ENTER:Send  DEL:Backspace  CLR:Cancel", 5, 220);
}

void takeInput(void) {
    const char* chars = "\0\0\0\0\0\0\0\0\0\0\"WRMH\0\0?[VQLG\0\0:ZUPKFC\0 YTOJEB\0\0XSNIDA\0\0\0\0\0\0\0\0";
    
    clearInput();
    inInputMode = true;
    
    drawInputScreen();
    gfx_SwapDraw();
    
    while (inInputMode) {
        uint8_t key = os_GetCSC();
        
        if (key == sk_Enter) {
            if (inputIndex > 0) {
                sendMessage();
            }
            inInputMode = false;
            break;
        } else if (key == sk_Clear) {
            inInputMode = false;
            break;
        } else if (key == sk_Del) {
            if (inputIndex > 0) {
                inputIndex--;
                inputBuffer[inputIndex] = '\0';
                drawInputScreen();
                gfx_SwapDraw();
            }
        } else if (chars[key] && inputIndex < INPUT_BUFFER_SIZE - 1) {
            inputBuffer[inputIndex++] = chars[key];
            inputBuffer[inputIndex] = '\0';
            drawInputScreen();
            gfx_SwapDraw();
        }
        
        delay(50);
    }
    
    // Auto-scroll to bottom
    int maxScroll = totalMessagesHeight - 190;
    if (maxScroll > 0) scrollOffset = maxScroll;
    
    drawMessages();
    gfx_SwapDraw();
}

void sendMessage(void) {
    if (!connected || inputIndex == 0) return;
    
    addMessage(inputBuffer, true);
    
    usb_BulkTransfer(global.in, inputBuffer, inputIndex, 0, NULL);
    
    waitingForResponse = true;
    clearInput();
}

void addMessage(const char* text, bool isUser) {
    Message* newMsg = malloc(sizeof(Message));
    if (!newMsg) return;
    
    newMsg->text = malloc(strlen(text) + 1);
    if (!newMsg->text) {
        free(newMsg);
        return;
    }
    
    strcpy(newMsg->text, text);
    newMsg->isUser = isUser;
    newMsg->next = NULL;
    
    if (messageTail == NULL) {
        messageHead = messageTail = newMsg;
    } else {
        messageTail->next = newMsg;
        messageTail = newMsg;
    }
}

void clearInput(void) {
    memset(inputBuffer, 0, INPUT_BUFFER_SIZE);
    inputIndex = 0;
}

void freeMessages(void) {
    Message* current = messageHead;
    while (current != NULL) {
        Message* next = current->next;
        free(current->text);
        free(current);
        current = next;
    }
    messageHead = messageTail = NULL;
}

static usb_error_t handleBulkOut(usb_endpoint_t endpoint, usb_transfer_status_t status,
                                  size_t transferred, usb_transfer_data_t* data) {
    if (status == USB_TRANSFER_COMPLETED && transferred > 0) {
        for (size_t i = 0; i < transferred && receiveIndex < BUFFER_SIZE - 1; i++) {
            char c = ((char*)data)[i];
            if (c != 0) {
                receiveBuffer[receiveIndex++] = c;
            }
        }
        receiveBuffer[receiveIndex] = '\0';
        
        char* newline = strchr(receiveBuffer, '\n');
        if (newline) {
            *newline = '\0';
            
            addMessage(receiveBuffer, false);
            
            receiveIndex = 0;
            memset(receiveBuffer, 0, BUFFER_SIZE);
            
            waitingForResponse = false;
            
            // Auto-scroll to bottom
            int maxScroll = totalMessagesHeight - 190;
            if (maxScroll > 0) scrollOffset = maxScroll;
            
            drawMessages();
            gfx_SwapDraw();
        }
    }
    
    return usb_ScheduleBulkTransfer(endpoint, data, 64, handleBulkOut, data);
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
            
            drawMessages();
            gfx_SwapDraw();
            break;
        }
        
        case USB_DEVICE_DISCONNECTED_EVENT:
            if (global_ptr->device == event_data) {
                global_ptr->device = NULL;
                global_ptr->in = global_ptr->out = NULL;
                connected = false;
                
                drawConnectingScreen();
                gfx_SwapDraw();
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
