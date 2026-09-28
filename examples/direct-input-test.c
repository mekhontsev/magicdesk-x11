#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/direct_input.c"
#include "../lorie/src/main/cpp/lorie/window_interaction.h"

DeviceIntPtr lorieTouch, loriePen, lorieEraser;
ScreenPtr pScreenPtr;
int ProximityIn = 66, ProximityOut = 67;
static DeviceIntRec touchDevice, penDevice, eraserDevice;
static int begins, ends, updates, enters, exits, presses, releases;
static uint32_t lastContact;
static double axes[6];

void valuator_mask_zero(ValuatorMask *mask) { memset(mask,0,sizeof(*mask)); }
void valuator_mask_set_double(ValuatorMask *mask,int axis,double value) { (void)mask; axes[axis]=value; }
void lorieSetStylusEnabled(Bool enabled) { assert(enabled); loriePen=&penDevice; lorieEraser=&eraserDevice; }
void QueueTouchEvents(DeviceIntPtr device,int type,uint32_t id,int flags,const ValuatorMask *mask) {
    (void)flags; (void)mask; assert(device==lorieTouch && id>0 && id<=20); lastContact=id;
    if(type==XI_TouchBegin) begins++;
    else if(type==XI_TouchUpdate) updates++;
    else { assert(type==XI_TouchEnd); ends++; }
}
void QueueProximityEvents(DeviceIntPtr device,int type,const ValuatorMask *mask) {
    (void)mask; assert(device==loriePen || device==lorieEraser);
    if(type==ProximityIn) enters++; else { assert(type==ProximityOut); exits++; }
}
void QueuePointerEvents(DeviceIntPtr device,int type,int button,int flags,const ValuatorMask *mask) {
    (void)flags; (void)mask; assert(device==loriePen || device==lorieEraser);
    if(type==MotionNotify) return;
    assert(button>=1 && button<=3);
    if(type==ButtonPress) presses++; else { assert(type==ButtonRelease); releases++; }
}

int main(void) {
    static ScreenRec screen; screen.width=1000; screen.height=500; pScreenPtr=&screen; lorieTouch=&touchDevice;
    LorieOutputCommand touch={.operation=LORIE_OUTPUT_TOUCH,.pressure=.5f,.detail=3,.phase=LORIE_TOUCH_BEGIN};
    lorieDirectInput(10,&touch,500,250);
    assert(begins==1 && axes[0]==32767.5 && axes[1]==32767.5 && axes[2]==32767.5);
    uint32_t first=lastContact;
    lorieDirectInput(20,&touch,100,100);
    assert(lastContact!=first && begins==2);
    lorieDirectInput(20,&touch,100,100); assert(begins==2);
    touch.phase=LORIE_TOUCH_UPDATE; lorieDirectInput(10,&touch,600,200); assert(updates==1);
    lorieDirectInputRelease(10); assert(ends==1 && !lorieDirectInputPressed(10) && lorieDirectInputPressed(20));
    lorieDirectInput(10,&touch,600,200); assert(updates==1);
    touch.phase=LORIE_TOUCH_END; lorieDirectInput(20,&touch,600,200); assert(ends==2);
    lorieDirectInputRelease(20); assert(ends==2);
    touch.phase=LORIE_TOUCH_BEGIN;
    for(int i=0;i<32;i++){touch.detail=i;lorieDirectInput(10,&touch,0,0);}
    assert(begins==22); lorieDirectInputRelease(10); assert(ends==22);

    LorieOutputCommand pen={.operation=LORIE_OUTPUT_TABLET,.pressure=.75f,.proximity=1,.buttons=3,.tiltX=.5f,.tiltY=-.5f};
    lorieDirectInput(10,&pen,500,250);
    assert(enters==1 && presses==2 && axes[2]==49151.25 && axes[3]>28 && axes[4]<-28);
    pen.eraser=1; lorieDirectInput(10,&pen,500,250);
    assert(enters==2 && exits==1 && releases==2 && presses==4);
    lorieDirectInputRelease(99); assert(releases==2);
    lorieDirectInputRelease(10); assert(releases==4 && exits==2 && !lorieDirectInputPressed(10));
    assert(lorieActivationTimestamp(105,100,110));
    assert(!lorieActivationTimestamp(0,100,110));
    assert(!lorieActivationTimestamp(99,100,110));
    assert(!lorieActivationTimestamp(111,100,110));
    assert(!lorieActivationTimestamp(105,100,20100));
    assert(lorieActivationTimestamp(4,UINT32_MAX-3,10));
    touch.detail=1; touch.phase=LORIE_TOUCH_BEGIN;
    lorieDirectInput(10,&touch,0,0); lorieDirectInput(10,&pen,0,0);
    assert(lorieDirectInputPressed(10));
    lorieDirectInputReset();
    assert(!lorieDirectInputPressed(10));
    unsigned previous=updates;
    touch.phase=LORIE_TOUCH_UPDATE; lorieDirectInput(10,&touch,0,0); assert(updates==previous);
    puts("direct input ownership, pressure, cancellation and activation timestamp checks passed");
}
