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
#define BUBBLE_MARGIN 8
#define BUBBLE_PADDING 4
#define LINE_HEIGHT 8
#define CHAR_WIDTH 4

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
int wrapText(const char* text, int maxWidth, char lines[][50], int maxLines);

int main(void) {
    usb_error_t error;
    
    memset(&global, 0, sizeof(global_t));
    memset(receiveBuffer, 0, BUFFER_SIZE);
    memset(inputBuffer, 0, INPUT_BUFFER_SIZE);
    
    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_SetTextTransparentColor(0);
    gfx_SetTextFGColor(255);
    
    drawConnectingScreen();
    gfx_BlitBuffer();
    
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
                        scrollOffset -= 20;
                        if (scrollOffset < 0) scrollOffset = 0;
                        drawMessages();
                        gfx_BlitBuffer();
                    }
                    delay(100);
                }
                if (kb_Data[7] & kb_Down) {
                    int maxScroll = totalMessagesHeight - (SCREEN_HEIGHT - 30);
                    if (scrollOffset < maxScroll && maxScroll > 0) {
                        scrollOffset += 20;
                        if (scrollOffset > maxScroll) scrollOffset = maxScroll;
                        drawMessages();
                        gfx_BlitBuffer();
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
    
    // Title box
    gfx_SetColor(31);
    gfx_FillRectangle(0, 0, SCREEN_WIDTH, 30);
    gfx_SetColor(255);
    gfx_Rectangle(0, 0, SCREEN_WIDTH, 30);
    
    gfx_SetTextXY(80, 10);
    gfx_PrintString("TI-84 ChatGPT");
    
    // Status
    gfx_SetTextXY(10, 50);
    if (connected) {
        gfx_PrintString("Status: Connected");
    } else {
        gfx_PrintString("Status: Connecting...");
    }
    
    gfx_SetTextXY(10, 70);
    gfx_PrintString("Plug in USB cable and");
    gfx_SetTextXY(10, 82);
    gfx_PrintString("connect from browser");
    
    // Credits
    gfx_SetColor(224);
    gfx_FillRectangle(0, SCREEN_HEIGHT - 40, SCREEN_WIDTH, 40);
    gfx_SetColor(255);
    gfx_Rectangle(0, SCREEN_HEIGHT - 40, SCREEN_WIDTH, 40);
    
    gfx_SetTextXY(90, SCREEN_HEIGHT - 28);
    gfx_SetTextFGColor(0);
    gfx_PrintString("Made by y4shg");
    gfx_SetTextFGColor(255);
    
    // Instructions
    gfx_SetTextXY(10, SCREEN_HEIGHT - 15);
    gfx_PrintString("CLEAR = Quit");
}

void drawMessages(void) {
    gfx_FillScreen(0);
    
    // Top bar
    gfx_SetColor(31);
    gfx_FillRectangle(0, 0, SCREEN_WIDTH, 20);
    gfx_SetColor(255);
    gfx_Rectangle(0, 0, SCREEN_WIDTH, 20);
    
    gfx_SetTextXY(5, 6);
    if (waitingForResponse) {
        gfx_PrintString("ChatGPT - Waiting...");
    } else {
        gfx_PrintString("ChatGPT - Ready");
    }
    
    // Bottom bar
    gfx_SetColor(31);
    gfx_FillRectangle(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    gfx_SetColor(255);
    gfx_Rectangle(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 15);
    
    gfx_SetTextXY(5, SCREEN_HEIGHT - 11);
    gfx_PrintString("2nd=Type  Arrows=Scroll  CLEAR=Quit");
    
    // Draw messages
    int yPos = 25 - scrollOffset;
    Message* msg = messageHead;
    totalMessagesHeight = 0;
    
    while (msg != NULL) {
        // Wrap text into lines
        char lines[20][50];
        int numLines = wrapText(msg->text, 35, lines, 20);
        
        int bubbleHeight = (numLines * LINE_HEIGHT) + (BUBBLE_PADDING * 2);
        int bubbleWidth = 0;
        
        // Calculate max width needed
        for (int i = 0; i < numLines; i++) {
            int lineWidth = strlen(lines[i]) * CHAR_WIDTH + BUBBLE_PADDING * 2;
            if (lineWidth > bubbleWidth) bubbleWidth = lineWidth;
        }
        
        if (bubbleWidth > 200) bubbleWidth = 200;
        
        int xPos;
        uint8_t bubbleColor;
        
        if (msg->isUser) {
            // User message (right side, blue)
            xPos = SCREEN_WIDTH - bubbleWidth - BUBBLE_MARGIN;
            bubbleColor = 160;  // Blue
        } else {
            // AI message (left side, green)
            xPos = BUBBLE_MARGIN;
            bubbleColor = 192;  // Green
        }
        
        // Only draw if visible on screen
        if (yPos + bubbleHeight > 20 && yPos < SCREEN_HEIGHT - 15) {
            // Draw bubble
            gfx_SetColor(bubbleColor);
            gfx_FillRectangle(xPos, yPos, bubbleWidth, bubbleHeight);
            gfx_SetColor(255);
            gfx_Rectangle(xPos, yPos, bubbleWidth, bubbleHeight);
            
            // Draw text
            for (int i = 0; i < numLines; i++) {
                gfx_SetTextXY(xPos + BUBBLE_PADDING, yPos + BUBBLE_PADDING + (i * LINE_HEIGHT));
                gfx_PrintString(lines[i]);
            }
        }
        
        yPos += bubbleHeight + 5;
        totalMessagesHeight = yPos + scrollOffset - 25;
        msg = msg->next;
    }
}

void drawInputScreen(void) {
    gfx_FillScreen(0);
    
    // Title
    gfx_SetColor(31);
    gfx_FillRectangle(0, 0, SCREEN_WIDTH, 25);
    gfx_SetColor(255);
    gfx_Rectangle(0, 0, SCREEN_WIDTH, 25);
    
    gfx_SetTextXY(80, 8);
    gfx_PrintString("Type Message");
    
    // Input box
    gfx_SetColor(64);
    gfx_FillRectangle(10, 35, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 70);
    gfx_SetColor(255);
    gfx_Rectangle(10, 35, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 70);
    
    // Draw input text with wrapping
    char lines[15][38];
    int numLines = wrapText(inputBuffer, 38, lines, 15);
    
    for (int i = 0; i < numLines && i < 15; i++) {
        gfx_SetTextXY(15, 40 + (i * LINE_HEIGHT));
        gfx_PrintString(lines[i]);
    }
    
    // Cursor
    int cursorLine = inputIndex / 38;
    int cursorCol = inputIndex % 38;
    if (cursorLine < 15) {
        gfx_SetColor(255);
        gfx_FillRectangle(15 + (cursorCol * CHAR_WIDTH), 40 + (cursorLine * LINE_HEIGHT), 2, LINE_HEIGHT);
    }
    
    // Instructions
    gfx_SetTextXY(10, SCREEN_HEIGHT - 25);
    gfx_PrintString("ENTER = Send    CLEAR = Cancel");
    gfx_SetTextXY(10, SCREEN_HEIGHT - 13);
    gfx_PrintString("DEL = Backspace");
}

void takeInput(void) {
    const char* chars = "\0\0\0\0\0\0\0\0\0\0\"WRMH\0\0?[VQLG\0\0:ZUPKFC\0 YTOJEB\0\0XSNIDA\0\0\0\0\0\0\0\0";
    
    clearInput();
    inInputMode = true;
    
    drawInputScreen();
    gfx_BlitBuffer();
    
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
                inputBuffer[inputIndex] = 0;
                drawInputScreen();
                gfx_BlitBuffer();
            }
        } else if (chars[key] && inputIndex < INPUT_BUFFER_SIZE - 1) {
            inputBuffer[inputIndex++] = chars[key];
            inputBuffer[inputIndex] = 0;
            drawInputScreen();
            gfx_BlitBuffer();
        }
        
        delay(50);
    }
    
    // Auto-scroll to bottom when new message added
    int maxScroll = totalMessagesHeight - (SCREEN_HEIGHT - 30);
    if (maxScroll > 0) scrollOffset = maxScroll;
    
    drawMessages();
    gfx_BlitBuffer();
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

int wrapText(const char* text, int maxWidth, char lines[][50], int maxLines) {
    int lineCount = 0;
    int textLen = strlen(text);
    int pos = 0;
    
    while (pos < textLen && lineCount < maxLines) {
        int lineLen = 0;
        int lastSpace = -1;
        
        while (pos + lineLen < textLen && lineLen < maxWidth) {
            if (text[pos + lineLen] == ' ') {
                lastSpace = lineLen;
            }
            lineLen++;
        }
        
        // If we didn't reach the end and there's a space, break at space
        if (pos + lineLen < textLen && lastSpace > 0) {
            lineLen = lastSpace;
        }
        
        strncpy(lines[lineCount], text + pos, lineLen);
        lines[lineCount][lineLen] = 0;
        
        pos += lineLen;
        if (pos < textLen && text[pos] == ' ') pos++; // Skip space
        
        lineCount++;
    }
    
    return lineCount;
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
        receiveBuffer[receiveIndex] = 0;
        
        char* newline = strchr(receiveBuffer, '\n');
        if (newline) {
            *newline = 0;
            
            addMessage(receiveBuffer, false);
            
            receiveIndex = 0;
            memset(receiveBuffer, 0, BUFFER_SIZE);
            
            waitingForResponse = false;
            
            // Auto-scroll to bottom
            int maxScroll = totalMessagesHeight - (SCREEN_HEIGHT - 30);
            if (maxScroll > 0) scrollOffset = maxScroll;
            
            drawMessages();
            gfx_BlitBuffer();
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
            gfx_BlitBuffer();
            break;
        }
        
        case USB_DEVICE_DISCONNECTED_EVENT:
            if (global_ptr->device == event_data) {
                global_ptr->device = NULL;
                global_ptr->in = global_ptr->out = NULL;
                connected = false;
                
                drawConnectingScreen();
                gfx_BlitBuffer();
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
