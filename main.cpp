

#include "stdio.h"
#include "scheduler_task.hpp"
#include "utilities.h"  //delay_ms()
#include "tasks.hpp"
//#include "examples/examples.hpp"
#include "wireless.h"
#include "constants.hpp"
#include "wireless/src/nrf24L01Plus.h"
#include "nrf_stream.hpp"
#include "event_groups.h"


#if SLAVE
bool motion_detected_flag = 0;

class motionDetector: public scheduler_task
{
    public:
        motionDetector(uint8_t priority) :
                scheduler_task("motion", 1048, priority)
        {

        }

        bool run(void* p)
        {
            int count = 0;
            const uint32_t motion = (1 << 20);


            LPC_GPIO1->FIODIR &= ~motion;      //reseting to make it work as an input for PIR input

            for (int i = 0; i < 1000; i++)
            {
                /* If output is low, then motion has been detected, but it could be false positive */
                //if (motion.read())
                if(LPC_GPIO1->FIOPIN & motion)
                {
                    count++;
                }
                delay_ms(1);
            }

            if(count > 100)
            {
                motion_detected_flag = 1;
            }

            if(motion_detected_flag)
            {
                DEBUG_PRINT("Motion Detected!\n");
            }
            else
            {
                DEBUG_PRINT("NO Motion! xxxxxxxxxxxxxxxxxx \n");
            }

            return true;
        }
};


class roomActivity: public scheduler_task
{
    public:
        roomActivity(uint8_t priority) :
                scheduler_task("roomActivity", 1048, priority)
        {

        }

        bool run(void* p)
        {
            uint8_t cmd = 0;

            if(motion_detected_flag)
            {
                motion_detected_flag = 0;
                cmd |= SETBIT(ROOM1) | SETBIT(MOTION);
                if (wireless_send(SERVER, mesh_pkt_nack, &cmd, 1, max_hops))
                {
                    printf("message sent to server -> %d\n", cmd);
                }
                else
                {
                    DEBUG_PRINT("Failed in sending packet\n");
                }

                delay_ms(INTERVAL);
            }
            else
            {
                cmd |= SETBIT(ROOM1);
                if (wireless_send(SERVER, mesh_pkt_nack, &cmd, 1, max_hops))
                {
                    printf("message sent to server -> %d\n", cmd);
                }
                else
                {
                    DEBUG_PRINT("Failed in sending packet\n");
                }
                delay_ms(INTERVAL);
            }

           return true;
        }
};

class nodeActivity: public scheduler_task
{
    public:
        nodeActivity(uint8_t priority) :
                scheduler_task("nodeActivity", 1048, priority)
        {

        }

        bool run(void* p)
        {
            mesh_packet_t pkt;
            char cmd;

            DEBUG_PRINT("Server is up and running\n");
            if (wireless_get_rx_pkt(&pkt, portMAX_DELAY))
            {
                cmd = pkt.data[0];
                DEBUG_PRINT("---------------------\n");
                printf("client sent me = %d\n", cmd);
                if(cmd & SETBIT(ROOM1))
                {
                    DEBUG_PRINT("Someone is in the room\n");
                }
                else
                {
                    DEBUG_PRINT("No one is in the room\n");
                }

                if(cmd & SETBIT(ROOM1))
                {
                    DEBUG_PRINT("Node is active and working\n");
                    DEBUG_PRINT("---------------------\n");
                }
            }

            return true;
        }
};

int main(void)
{
    wireless_init();

    scheduler_add_task(new wirelessTask(PRIORITY_CRITICAL));
    //scheduler_add_task(new terminalTask(PRIORITY_HIGH));
    scheduler_add_task(new roomActivity(PRIORITY_HIGH));
    //scheduler_add_task(new nodeActivity(PRIORITY_MEDIUM));
    scheduler_add_task(new motionDetector(PRIORITY_HIGH));

    scheduler_start();
    return -1;
}

#else

class server: public scheduler_task
{
    public:
        server(uint8_t priority) :
                scheduler_task("server", 1048, priority)
        {
            EventGroupHandle_t xEventGroup = xEventGroupCreate();
            addSharedObject(shared_eventGroup, xEventGroup);
        }

        bool run(void* p)
        {
            mesh_packet_t pkt;
            char cmd;
            uint32_t roomNo;
            EventBits_t bitStatus = 0;

            DEBUG_PRINT("Server is up and running\n");

            if (wireless_get_rx_pkt(&pkt, portMAX_DELAY))
            {
                cmd = pkt.data[0];

                DEBUG_PRINT("---------------------\n");

                roomNo = (cmd & 0xF0) >> 4;
                if(roomNo == 4)
                {
                    roomNo = 3;
                }
                printf(SEND_BOARD_ACTIVE, roomNo);

                delay_ms(1000);

                if(cmd & SETBIT(MOTION))
                {
                    printf(SEND_ROOM_ACTIVE,roomNo);
                }
                else
                {
                    printf(SEND_ROOM_INACTIVE,roomNo);
                }

                switch(roomNo)
                {
                    case 1: bitStatus = xEventGroupSetBits(getSharedObject(shared_eventGroup),SETBIT(0));
                            break;
                    case 2: bitStatus = xEventGroupSetBits(getSharedObject(shared_eventGroup),SETBIT(1));
                            break;
                    case 3: bitStatus = xEventGroupSetBits(getSharedObject(shared_eventGroup),SETBIT(2));
                            break;
                    default: DEBUG_PRINT("Invalid case\n");
                            break;
                }
                //printf("SERVER: event grp bit status is: %x\n", bitStatus);//comment this line after debug
            }
            return true;
        }
};

class task_watchdog: public scheduler_task
{
    public:
        task_watchdog(uint8_t priority) :
                scheduler_task("server", 1048, priority)
        {

        }

        bool run(void *p)
        {
            EventBits_t bitStatus = 0;
            const TickType_t xTicksToWait = 100;

            DEBUG_PRINT("Watchdog: Don't Worry...Let me check who is sleeping\n");

            bitStatus = xEventGroupWaitBits(getSharedObject(shared_eventGroup),
                                            SETBIT(0)|SETBIT(1)|SETBIT(2),
                                            1,0,xTicksToWait);

            //printf("event grp bit status is: %x\n", bitStatus);//comment this line after debug

            if((bitStatus & SETBIT(0)) == 0)
            {
                printf(SEND_BOARD_INACTIVE, 1);
            }

            if((bitStatus & SETBIT(1)) == 0)
            {
                printf(SEND_BOARD_INACTIVE, 2);
            }

            if((bitStatus & SETBIT(2)) == 0)
            {
                printf(SEND_BOARD_INACTIVE, 3);
            }

            delay_ms(WATCHDOG);

            return true;
        }
};

int main(void)
{
    wireless_init();

    scheduler_add_task(new wirelessTask(PRIORITY_CRITICAL));
    //scheduler_add_task(new terminalTask(PRIORITY_HIGH));
    scheduler_add_task(new server(PRIORITY_MEDIUM));
    scheduler_add_task(new task_watchdog(PRIORITY_HIGH));

    scheduler_start();
    return -1;
}

#endif
