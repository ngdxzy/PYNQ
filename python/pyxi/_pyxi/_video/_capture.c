/******************************************************************************
*
* Copyright (C) 2010 - 2015 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* XILINX CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

/*
 * CPython bindings for a video capture peripheral (video_capture.h)
 *
 * @author Giuseppe Natale <giuseppe.natale@xilinx.com>
 * @date   27 JAN 2016
 */

#include <Python.h>         //pulls the Python API
#include <structmember.h>   //handle attributes

#include <stdio.h>
#include <stdlib.h>
#include "video_commons.h"
#include "video_capture.h"

#include "_video.h"


typedef struct{
    PyObject_HEAD
    VideoCapture *capture;
    videoframeObject *frame;
} videocaptureObject;


/*****************************************************************************/
/* Defining OOP special methods                                              */

/*
 * deallocator
 */
static void videocapture_dealloc(videocaptureObject* self){
    Py_Del_XAxiVdma(self->capture->vdma);
    Py_Del_XVtc(&(self->capture->vtc));
    Py_Del_XGpio(self->capture->gpio);
    free(self->capture);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

/*
 * __new()__ method
 */
static PyObject *videocapture_new(PyTypeObject *type, PyObject *args, 
                                  PyObject *kwds){
    videocaptureObject *self;
    self = (videocaptureObject *)type->tp_alloc(type, 0);
    if((self->capture = (VideoCapture *)malloc(sizeof(VideoCapture))) == NULL){
        PyErr_Format(PyExc_MemoryError, "unable to allocate memory");
        return NULL;        
    }
    return (PyObject *)self;
}

/*
 * __init()__ method
 *
 * Python Constructor:  capture(vdma_dict,gpio_dict,vtcBaseAddress,
                                [video.frame])
 */
static int videocapture_init(videocaptureObject *self, PyObject *args){
    self->frame = NULL;
    PyObject *vdma_dict = NULL, *gpio_dict = NULL;
    unsigned int vtcBaseAddress;
    if (!PyArg_ParseTuple(args, "OOI|O", &vdma_dict, &gpio_dict,  
                          &vtcBaseAddress, &self->frame))
        return -1;
    if (!(PyDict_Check(vdma_dict) && PyDict_Check(gpio_dict)))
        return -1;

    if(self->frame == NULL){ //create new
        self->frame = PyObject_New(videoframeObject, &videoframeType);
        for(int i = 0; i < NUM_FRAMES; i++)
            if((self->frame->frame_buffer[i] = 
                (u8 *)frame_alloc(sizeof(u8)*MAX_FRAME)) == NULL){
                PyErr_Format(PyExc_MemoryError, "unable to allocate memory");
                return -1;            
            }
    }

    int Status = VideoInitialize(self->capture, vdma_dict, gpio_dict, 
                                 vtcBaseAddress, self->frame->frame_buffer, 
                                 STRIDE);
    if (Status != XST_SUCCESS){
        PyErr_Format(PyExc_LookupError, 
                     "video.capture initialization failed [%d]", Status);
        return -1;
    }
    return 0;

}


/*
 * __str()__ method
 */
static PyObject *videocapture_str(videocaptureObject *self){
    VtcDetect(self->capture);
    char str[200];
    sprintf(str, "Video Capture \r\n   State: %d \r\n   Current Index: %d \r\n   Current Width: %d \r\n   Current Height: %d", 
            self->capture->state, self->capture->curFrame, 
            self->capture->timing.HActiveVideo, 
            self->capture->timing.HActiveVideo);
    return Py_BuildValue("s",str);
}

/*
 * exposing members
 */
static PyMemberDef videocapture_members[] = {
    {"framebuffer", T_OBJECT, offsetof(videocaptureObject, frame), READONLY,
     "FrameBuffer object"},
    {NULL}  /* Sentinel */
};

/*****************************************************************************/
/* Actual C bindings - member functions                                      */

/*
 * frame_index([new_index])
 * get current index or if the argument is specified set it to a new one
 * within the allowed range
 */
static PyObject *videocapture_frame_index(videocaptureObject *self, 
                                          PyObject *args){
    Py_ssize_t nargs = PyTuple_Size(args);
    if(nargs > 0){
        unsigned int newIndex = 0;
        if (!PyArg_ParseTuple(args, "I", &newIndex))
            return NULL;
        if(newIndex >= 0 && newIndex < NUM_FRAMES){       
            self->capture->curFrame = newIndex;
            VideoChangeFrame(self->capture, newIndex);
            Py_RETURN_NONE;
        }
        else{
            PyErr_Format(PyExc_ValueError, 
                         "index %d out of range [%d,%d]",
                         newIndex, 0, NUM_FRAMES-1);
            return NULL;
        }
    }
    return Py_BuildValue("I", self->capture->curFrame);
}


/*
 * frame_index_next()
 * Set the frame index to the next one and return it
 */
static PyObject *videocapture_frame_index_next(videocaptureObject *self){
    unsigned int newIndex = self->capture->curFrame + 1;
     if(newIndex >= NUM_FRAMES)
        newIndex = 0;         
    VideoChangeFrame(self->capture, newIndex);   
    return Py_BuildValue("I", self->capture->curFrame);
}

/*
 * frame_width()
 * get current width
 */
static PyObject *videocapture_frame_width(videocaptureObject *self){
    VtcDetect(self->capture);
    return Py_BuildValue("I", self->capture->timing.HActiveVideo);
}

/*
 * frame_height()
 * get current height
 */
static PyObject *videocapture_frame_height(videocaptureObject *self){
    VtcDetect(self->capture);
    return Py_BuildValue("I", self->capture->timing.VActiveVideo);
}

/*
 * start()
 */
static PyObject *videocapture_start(videocaptureObject *self){
    VideoStart(self->capture);
    Py_RETURN_NONE;
}

/*
 * stop()
 */
static PyObject *videocapture_stop(videocaptureObject *self){
    VideoStop(self->capture);
    Py_RETURN_NONE;
}

/*
 * state()
 */
static PyObject *videocapture_state(videocaptureObject *self){
    return Py_BuildValue("I", self->capture->state);
}


/*
 * frame([index])
 * 
 * just a wrapper of get_frame() and set_frame() defined for the videoframe
 * object. supports only read_mode.
 */
static PyObject *videocapture_frame(videocaptureObject *self, PyObject *args){
    unsigned int index = self->capture->curFrame;
    Py_ssize_t nargs = PyTuple_Size(args);
    if(nargs == 0 || (nargs == 1 && PyArg_ParseTuple(args, "I", &index))){
        return get_frame(self->frame, index);
    }
    else{
        PyErr_Clear(); //clear possible exception set by PyArg_ParseTuple
        PyErr_SetString(PyExc_SyntaxError, "invalid argument");
        return NULL;        
    }
}

/*****************************************************************************/

/*
 * defining the methods
 *
 */
static PyMethodDef videocapture_methods[] = {
    {"frame_index", (PyCFunction)videocapture_frame_index, METH_VARARGS,
     "Get current index or if the argument is specified set it to a new one within the allowed range."
    },
    {"frame_index_next", (PyCFunction)videocapture_frame_index_next, METH_VARARGS,
     "Set the frame index to the next one and return it."
    },
    {"frame_width", (PyCFunction)videocapture_frame_width, METH_VARARGS,
     "Get the current frame width."
    },
    {"frame_height", (PyCFunction)videocapture_frame_height, METH_VARARGS,
     "Get the current frame height."
    },
    {"start", (PyCFunction)videocapture_start, METH_VARARGS,
     "Start the video capture controller."
    },
    {"stop", (PyCFunction)videocapture_stop, METH_VARARGS,
     "Stop the video capture controller."
    },
    {"state", (PyCFunction)videocapture_state, METH_VARARGS,
     "Get the state of the video capture controller."
    },
    {"frame", (PyCFunction)videocapture_frame, METH_VARARGS,
     "Get the current frame (or the one at 'index' if specified)."
    },
    {NULL}  /* Sentinel */
};

/*****************************************************************************/
/* Defining the type object                                                  */

PyTypeObject videocaptureType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_video._capture",                          /* tp_name */
    sizeof(videocaptureObject),                 /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)videocapture_dealloc,           /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_reserved */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash  */
    0,                                          /* tp_call */
    (reprfunc)videocapture_str,                 /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    "Video Capture object",                     /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    videocapture_methods,                       /* tp_methods */
    videocapture_members,                       /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    (initproc)videocapture_init,                /* tp_init */
    0,                                          /* tp_alloc */
    videocapture_new,                           /* tp_new */
};