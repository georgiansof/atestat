//#define GRAPHICSMODE

#include <gui/colors.h>
#include <common/types.h>
#include <common/containers/list.h>
#include <common/containers/deque.h>
#include <common/containers/stack.h>
#include <common/containers/queue.h>
#include <common/containers/bitset.h>
#include <gdt.h>
#include <memorymanagement.h>
#include <hardwarecomm/interrupts.h>
#include <hardwarecomm/pci.h>
#include <drivers/driver.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/vga.h>
#include <drivers/ata.h>
#include <gui/desktop.h>
#include <gui/window.h>
#include <multitasking.h>

using namespace blockos;
using namespace blockos::common;
using namespace blockos::hardwarecomm;
using namespace blockos::drivers;
using namespace blockos::gui;
using namespace blockos::common::containers;

bool virtualmode;

int StrLen(char *s)
{
    int n=0;
    for(int i=0;s[i];++i)
        ++n;
    return n;
}
void printf(const char* str)
{
#ifndef GRAPHICSMODE
    static volatile uint16_t* VideoMemory = (uint16_t*)0xb8000; 
    static uint8_t x=0, y=0;
    
    for(int i=0;str[i] != '\0'; ++i)
    {
        
        switch(str[i])
        {
            case '\n':
                ++y,x=0;
                break;

            default:
            VideoMemory[80*y+x] = (VideoMemory[80*y+x] & 0xFF00) | str[i];
            ++x;
        }
        
        if(x >= 80)
        {
            ++y;
            x=0;
        }
        
        if(y >= 25)
        {
            for(y=0;y<25;++y)
                for(x=0;x<80;++x)
                    VideoMemory[80*y+x] = (VideoMemory[80*y+x] & 0xFF00) | ' ';
                    
            x=0;
            y=0;
        }
    }
#endif
}
void printf(char c)
{
#ifndef GRAPHICSMODE
    char msg[2];
    msg[0]=c;
    msg[1]=null;
    printf(msg);
#endif
}
void printf(int32_t nr)
{
#ifndef GRAPHICSMODE
    char str[12];
    int strlen=-1;
    if(nr==0)
    {
        str[0]='0';
        str[1]=null;
        printf(str);
        return;
    }
    while(nr)
    {
        str[++strlen]=(nr%10)+'0';
        nr/=10;
    }
    for(int i=0;i<(strlen+1)/2;++i)
        Swap(str[i],str[strlen-i]);
    str[strlen+1]=NULL;
    printf(str);
#endif
}
void printf(bool x)
{
#ifndef GRAPHICSMODE
    char str[2];
    if(x==0) str[0]='0';
    if(x==1) str[0]='1';
    str[1]=NULL;
    printf(str);
#endif
}
void printfHex(uint8_t key)
{
#ifndef GRAPHICSMODE
    char* msg = "00";
    char* hex = "0123456789ABCDEF";
    msg[0] = hex[(key >> 4) & 0x0F];
    msg[1] = hex[key & 0x0F];
    printf(msg); 
#endif
}

#ifndef GRAPHICSMODE // cout, mousehandler, kbhandler
class outStream
{
    public:
    void operator <<(const char* str) {printf(str);}
    void operator <<(int32_t number) {printf(number);}
    void operator <<(bool number) {printf(number);}
    void operator <<(char c)
    {
        char str[2];
        str[0]=c;
        str[1]=NULL;
        printf(str);
    }
} cout;

class PrintfKeyboardEventHandler : public KeyboardEventHandler
{
public:
    void OnKeyDown(char c)
    {
        char* str = " ";
        str[0]=c;
        printf(str);
    }
};

class MouseToConsole : public MouseEventHandler
{
    int8_t x,y;
public:
    MouseToConsole()
    {
        uint16_t* VideoMemory = (uint16_t*)0xb8000;
        x=40;
        y=12;
        VideoMemory[80*y+x] = ((VideoMemory[80*y+x] & 0xF000) >> 4) 
                            | ((VideoMemory[80*y+x] & 0x0F00) << 4) 
                            | ((VideoMemory[80*y+x] & 0x00FF)); 
    }
    void OnMouseMove(int xoffset, int yoffset)
    {
        static volatile uint16_t* VideoMemory = (uint16_t*)0xb8000; 
        
        VideoMemory[80*y+x] = ((VideoMemory[80*y+x] & 0xF000) >> 4) 
                            | ((VideoMemory[80*y+x] & 0x0F00) << 4) 
                            | ((VideoMemory[80*y+x] & 0x00FF)); 
        
        x += xoffset;
        if(x < 0) x = 0;
        if(x >=80) x=79;
        
        y += yoffset;
        if(y < 0) y=0;
        if(y>=25) y=24;
        
        VideoMemory[80*y+x] = ((VideoMemory[80*y+x] & 0xF000) >> 4) 
                            | ((VideoMemory[80*y+x] & 0x0F00) << 4) 
                            | ((VideoMemory[80*y+x] & 0x00FF)); 
    }
    void OnMouseDown(uint8_t button)
    {
        if(button==1) printf("LMB_DOWN");
        if(button==2) printf("RMB_DOWN");
        if(button==3) printf("WMB_DOWN");
    }
    void OnMouseUp(uint8_t button)
    {
        if(button==1) printf("LMB_UP");
        if(button==2) printf("RMB_UP");
        if(button==3) printf("WMB_UP");
    }
};
#endif


void taskA()
{
    while(true)
        printf("A");
}
void taskB()
{
    while(true)
        printf("B");
}

typedef void (*constructor)();
extern "C" constructor *start_ctors;
extern "C" constructor *end_ctors;
extern "C" void callConstructors()
{
    for(constructor *i= start_ctors; i!=end_ctors; ++i)
        (*i)();
}

extern "C" void kernelMain(void* multiboot_structure, uint32_t /*multiboot_magic*/)
{
    bool open=1;
    printf("Successfully booted BlockOS!\n\n");
    
    GlobalDescriptorTable gdt;
    /************************ Allocating heap ******************/
    uint32_t* memupper = (uint32_t*)(((size_t)multiboot_structure) + 8);
    size_t heap = 10*1024*1024;
    MemoryManager memoryManager(heap, (*memupper)*1024 - heap - 10*1024);
    void* allocated = memoryManager.malloc(1024);
    printf("Heap initialised\n");
    /************** Multitasking **************/
    TaskManager taskManager;
    printf("Multitasking module initialized\n");
    /*Task task1(&gdt, taskA);
    Task task2(&gdt, taskB);
    taskManager.AddTask(&task1);
    taskManager.AddTask(&task2);*/
    /************* Drivers and interrupts *************/
    InterruptManager interrupts(0x20, &gdt, &taskManager);
#ifdef GRAPHICSMODE
    Desktop desktop(320,200,0x00,0x00,0xA8);
#endif
    printf("Initializing drivers...\n");
    
    DriverManager drvManager;
    

#ifdef GRAPHICSMODE
    KeyboardDriver keyboard(&interrupts, &desktop);
#else 
    PrintfKeyboardEventHandler kbhandler;
    KeyboardDriver keyboard(&interrupts, &kbhandler);
#endif
    drvManager.AddDriver(&keyboard);
	PeripherialComponentInterconnectController PCIcontroller(&virtualmode);
	PCIcontroller.SelectDrivers(&drvManager, &interrupts);
#ifdef GRAPHICSMODE
    MouseDriver mouse(&interrupts, &desktop,&virtualmode);
#else
    MouseToConsole mousehandler;
    MouseDriver mouse(&interrupts, &mousehandler,&virtualmode);
#endif
    drvManager.AddDriver(&mouse);
        
#ifdef GRAPHICSMODE
    VideoGraphicsArray vga;
    desktop.gc=&vga;
#endif

    drvManager.ActivateAll();
    
    /************** VIDEO MODE INIT ************/
#ifdef GRAPHICSMODE
    vga.SetMode(320,200,8);

    Window *win1= new Window(&desktop, 10,10,25,25, DARK_MAGENTA);
    desktop.AddChild(win1);
    Window *win2 = new Window(&desktop, 40,15,30,30, 0x00, 0xA8, 0x00);
    desktop.AddChild(win2);

#endif
    /************** Storage drivers ************/
    // interrupt 14
    AdvancedTechnologyAttachment ata0m(0x1F0, true);
    AdvancedTechnologyAttachment ata0s(0x1F0, false);
    printf("Initializing storage... Testing: \n");
    char* atabuffer = "Storage driver test - BlockOs\n";
    ata0s.Write28(0,(uint8_t*)atabuffer,StrLen(atabuffer));
    //ata0s.Flush(); - modified to automatically flush after Write. Not flushing breaks the sector !

    ata0s.Read28(0,32);

    // interrupt 15
    AdvancedTechnologyAttachment ata1m(0x170, false);
    AdvancedTechnologyAttachment ata1s(0x170, false);

    // third: 0x1E8, int?
    // fourth: 0x168, int?
    /************* Pass control to user **************/
    printf("All set!\n");
    interrupts.Activate();
    
    while(open)
    {
#ifdef GRAPHICSMODE
        desktop.Draw();
#endif
    }
}
