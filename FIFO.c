/* Very simple queue
 * These are FIFO queues which discard the new data when full.
 *
 * Queue is empty when in == out.
 * If in != out, then 
 *  - items are placed into in before incrementing in
 *  - items are removed from out before incrementing out
 * Queue is full when in == (out-1 + QUEUE_SIZE) % QUEUE_SIZE;
 *
 * The queue will hold QUEUE_ELEMENTS number of items before the
 * calls to QueuePut fail.
 * http://stackoverflow.com/questions/215557/most-elegant-way-to-implement-a-circular-list-fifo
 */


#include <avr/io.h>
#include <stdlib.h>
#include <avr/interrupt.h>

/* Queue structure */
#define QUEUE_ELEMENTS 100
#define QUEUE_SIZE (QUEUE_ELEMENTS + 1)
uint8_t midi_queue[QUEUE_SIZE];
uint8_t midi_queue_in, midi_queue_out;

void midi_queue_init(void)
{
    midi_queue_in = midi_queue_out = 0;
}

int midi_queue_put(uint8_t new)
{
    if(midi_queue_in == (( midi_queue_out - 1 + QUEUE_SIZE) % QUEUE_SIZE))
    {
        return -1; /* Queue Full*/
    }

    midi_queue[midi_queue_in] = new;

    midi_queue_in = (midi_queue_in + 1) % QUEUE_SIZE;

    return 0; // No errors
}

int midi_queue_get(uint8_t *old)
{
    if(midi_queue_in == midi_queue_out)
    {
        return -1; /* Queue Empty - nothing to get*/
    }

    *old = midi_queue[midi_queue_out];

    midi_queue_out = (midi_queue_out + 1) % QUEUE_SIZE;

    return 0; // No errors
}