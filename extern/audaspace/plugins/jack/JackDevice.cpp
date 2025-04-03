/*******************************************************************************
 * Copyright 2009-2016 Jörg Müller
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#include "JackDevice.h"
#include "JackLibrary.h"
#include "devices/DeviceManager.h"
#include "devices/IDeviceFactory.h"
#include "Exception.h"

#include <cstring>
#include <algorithm>

AUD_NAMESPACE_BEGIN

void JackDevice::updateRingBuffers()
{
	size_t size, temp;
	unsigned int samplesize = AUD_SAMPLE_SIZE(m_specs);
	unsigned int i, j;
	unsigned int channels = m_specs.channels;
	sample_t* buffer = m_buffer.getBuffer();
	float* deinterleave = m_deinterleavebuf.getBuffer();
	jack_transport_state_t state;
	jack_position_t position;

	std::unique_lock<std::mutex> lock(m_mixingLock);

	while(m_valid)
	{
		state = AUD_jack_transport_query(m_client, &position);

		// we sync either when:
		// - there was a jack sync callback that requests a playing sync (either start playback or seek during playback) - caused by a m_syncCallRevision change in jack_sync
		// - the jack transport state changed to stop from not stopped (i.e. external stopping) - checked here
		// - the sync time changes when seeking during the stopped state - caused by a m_syncCallRevision change in jack_mix
		if((m_syncCallRevision != m_lastSyncCallRevision) || (state == JackTransportStopped && m_lastState != JackTransportStopped))
		{
			int syncRevision = m_syncCallRevision;
			float syncTime = m_syncTime;

			if(m_syncFunc)
				m_syncFunc(m_syncFuncData, state != JackTransportStopped, syncTime);

			// we reset the ring buffers when we sync to start from the correct position
			for(i = 0; i < channels; i++)
				AUD_jack_ringbuffer_reset(m_ringbuffers[i]);

			m_lastSyncCallRevision = syncRevision;
		}

		m_lastState = state;

		size = AUD_jack_ringbuffer_write_space(m_ringbuffers[0]);
		for(i = 1; i < channels; i++)
			if((temp = AUD_jack_ringbuffer_write_space(m_ringbuffers[i])) < size)
				size = temp;

		while(size > samplesize)
		{
			size /= samplesize;
			mix((data_t*)buffer, size);
			for(i = 0; i < channels; i++)
			{
				for(j = 0; j < size; j++)
					deinterleave[i * size + j] = buffer[i + j * channels];
				AUD_jack_ringbuffer_write(m_ringbuffers[i], (char*)(deinterleave + i * size), size * sizeof(float));
			}

			size = AUD_jack_ringbuffer_write_space(m_ringbuffers[0]);
			for(i = 1; i < channels; i++)
				if((temp = AUD_jack_ringbuffer_write_space(m_ringbuffers[i])) < size)
					size = temp;
		}

		m_mixingCondition.wait(lock);
	}
}

int JackDevice::jack_mix(jack_nframes_t length, void* data)
{
	JackDevice* device = (JackDevice*)data;
	unsigned int i;
	int count = device->m_specs.channels;
	char* buffer;

	jack_position_t position;
	jack_transport_state_t state = AUD_jack_transport_query(device->m_client, &position);

	if(state == JackTransportStarting)
	{
		// play silence while syncing
		for(unsigned int i = 0; i < count; i++)
			std::memset(AUD_jack_port_get_buffer(device->m_ports[i], length), 0, length * sizeof(float));
	}
	else
	{
		// ensure that if two consecutive seeks to exactly the same position result in a sync callback call in jack_sync
		if((state == JackTransportRolling) && (device->m_lastMixState != JackTransportRolling))
			++device->m_rollingSyncRevision;

		size_t temp;
		size_t readsamples = AUD_jack_ringbuffer_read_space(device->m_ringbuffers[0]);
		for(i = 1; i < count; i++)
			if((temp = AUD_jack_ringbuffer_read_space(device->m_ringbuffers[i])) < readsamples)
				readsamples = temp;

		readsamples = std::min(readsamples / sizeof(float), size_t(length));

		for(unsigned int i = 0; i < count; i++)
		{
			buffer = (char*)AUD_jack_port_get_buffer(device->m_ports[i], length);
			AUD_jack_ringbuffer_read(device->m_ringbuffers[i], buffer, readsamples * sizeof(float));
			if(readsamples < length)
				std::memset(buffer + readsamples * sizeof(float), 0, (length - readsamples) * sizeof(float));
		}

		// if we are stopped and the jack transport position changes, we need to notify the mixing thread to call the sync callback
		if(state == JackTransportStopped)
		{
			float syncTime = position.frame / (float) position.frame_rate;

			if(syncTime != device->m_syncTime)
			{
				device->m_syncTime = syncTime;
				++device->m_syncCallRevision;
			}
		}

		device->m_mixingCondition.notify_all();
	}

	device->m_lastMixState = state;

	return 0;
}

int JackDevice::jack_sync(jack_transport_state_t state, jack_position_t* pos, void* data)
{
	JackDevice* device = (JackDevice*)data;

	// we return immediately when the state is stopped as this is handled in the mixing thread separately, as not all stops result in a call here from jack.
	if(state == JackTransportStopped)
		return 1;

	float syncTime = pos->frame / (float) pos->frame_rate;

	// We need to call the sync callback in the mixing thread if
	// - the sync time is different, i.e., a new sync to a different time is done
	// - if the last state is stopped, i.e., we are starting playback
	// - if the sync time is the same but the rolling revision is increased, i.e., we are syncing repeatedly to the same time (happens especially when jumping back to the start)
	if((syncTime != device->m_syncTime) || (device->m_lastMixState == JackTransportStopped) || (device->m_rollingSyncRevision != device->m_lastRollingSyncRevision))
	{
		device->m_syncTime = syncTime;
		++device->m_syncCallRevision;
		device->m_mixingCondition.notify_all();
		device->m_lastRollingSyncRevision = device->m_rollingSyncRevision;
		return 0;
	}

	return device->m_syncCallRevision == device->m_lastSyncCallRevision;
}

void JackDevice::jack_shutdown(void* data)
{
	JackDevice* device = (JackDevice*)data;
	device->m_valid = false;
}

JackDevice::JackDevice(const std::string& name, DeviceSpecs specs, int buffersize)
{
	if(specs.channels == CHANNELS_INVALID)
		specs.channels = CHANNELS_STEREO;

	// jack uses floats
	m_specs = specs;
	m_specs.format = FORMAT_FLOAT32;

	jack_options_t options = JackNullOption;
	jack_status_t status;

	// open client
	m_client = AUD_jack_client_open(name.c_str(), options, &status);
	if(m_client == nullptr)
		AUD_THROW(DeviceException, "Connecting to the JACK server failed.");

	// set callbacks
	AUD_jack_set_process_callback(m_client, JackDevice::jack_mix, this);
	AUD_jack_on_shutdown(m_client, JackDevice::jack_shutdown, this);
	AUD_jack_set_sync_callback(m_client, JackDevice::jack_sync, this);

	// register our output channels which are called ports in jack
	m_ports = new jack_port_t*[m_specs.channels];

	try
	{
		char portname[64];
		for(int i = 0; i < m_specs.channels; i++)
		{
			sprintf(portname, "out %d", i+1);
			m_ports[i] = AUD_jack_port_register(m_client, portname, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
			if(m_ports[i] == nullptr)
				AUD_THROW(DeviceException, "Registering output port with JACK failed.");
		}
	}
	catch(Exception&)
	{
		AUD_jack_client_close(m_client);
		delete[] m_ports;
		throw;
	}

	m_specs.rate = (SampleRate)AUD_jack_get_sample_rate(m_client);

	buffersize *= sizeof(sample_t);
	m_ringbuffers = new jack_ringbuffer_t*[specs.channels];
	for(unsigned int i = 0; i < specs.channels; i++)
		m_ringbuffers[i] = AUD_jack_ringbuffer_create(buffersize);
	buffersize *= specs.channels;
	m_deinterleavebuf.resize(buffersize);
	m_buffer.resize(buffersize);

	create();

	m_lastState = JackTransportStopped;
	m_lastMixState = JackTransportStopped;
	m_valid = true;
	m_syncFunc = nullptr;
	m_syncTime = 0;
	m_syncCallRevision = 0;
	m_lastSyncCallRevision = 0;
	m_rollingSyncRevision = 0;
	m_lastRollingSyncRevision = 0;

	// activate the client
	if(AUD_jack_activate(m_client))
	{
		AUD_jack_client_close(m_client);
		delete[] m_ports;
		for(unsigned int i = 0; i < specs.channels; i++)
			AUD_jack_ringbuffer_free(m_ringbuffers[i]);
		delete[] m_ringbuffers;
		destroy();

		AUD_THROW(DeviceException, "Client activation with JACK failed.");
	}

	const char** ports = AUD_jack_get_ports(m_client, nullptr, nullptr,
										JackPortIsPhysical | JackPortIsInput);
	if(ports != nullptr)
	{
		for(int i = 0; i < m_specs.channels && ports[i]; i++)
			AUD_jack_connect(m_client, AUD_jack_port_name(m_ports[i]), ports[i]);

		AUD_jack_free(ports);
	}

	m_mixingThread = std::thread(&JackDevice::updateRingBuffers, this);
}

JackDevice::~JackDevice()
{
	if(m_valid)
		AUD_jack_client_close(m_client);
	m_valid = false;

	delete[] m_ports;

	m_mixingCondition.notify_all();

	m_mixingThread.join();

	for(unsigned int i = 0; i < m_specs.channels; i++)
		AUD_jack_ringbuffer_free(m_ringbuffers[i]);
	delete[] m_ringbuffers;

	destroy();
}

void JackDevice::playing(bool playing)
{
	// Do nothing.
}

void JackDevice::playSynchronizer()
{
	AUD_jack_transport_start(m_client);
}

void JackDevice::stopSynchronizer()
{
	AUD_jack_transport_stop(m_client);
}

void JackDevice::seekSynchronizer(double time)
{
	if(time >= 0.0f)
		AUD_jack_transport_locate(m_client, time * m_specs.rate);
}

void JackDevice::setSyncCallback(syncFunction sync, void* data)
{
	m_syncFunc = sync;
	m_syncFuncData = data;
}

double JackDevice::getSynchronizerPosition()
{
	jack_position_t position;
	jack_transport_state_t state = AUD_jack_transport_query(m_client, &position);
	double result = position.frame / (double) position.frame_rate;

	if(state == JackTransportRolling)
	{
		result += AUD_jack_frames_since_cycle_start(m_client) / (double) position.frame_rate;
	}

	return result;
}

int JackDevice::isSynchronizerPlaying()
{
	return AUD_jack_transport_query(m_client, nullptr);
}

class JackDeviceFactory : public IDeviceFactory
{
private:
	DeviceSpecs m_specs;
	int m_buffersize;
	std::string m_name;

public:
	JackDeviceFactory() :
		m_buffersize(AUD_DEFAULT_BUFFER_SIZE),
		m_name("Audaspace")
	{
		m_specs.format = FORMAT_FLOAT32;
		m_specs.channels = CHANNELS_STEREO;
		m_specs.rate = RATE_48000;
	}

	virtual std::shared_ptr<IDevice> openDevice()
	{
		return std::shared_ptr<IDevice>(new JackDevice(m_name, m_specs, m_buffersize));
	}

	virtual int getPriority()
	{
		return 0;
	}

	virtual void setSpecs(DeviceSpecs specs)
	{
		m_specs = specs;
	}

	virtual void setBufferSize(int buffersize)
	{
		m_buffersize = buffersize;
	}

	virtual void setName(const std::string &name)
	{
		m_name = name;
	}
};

void JackDevice::registerPlugin()
{
	if(loadJACK())
		DeviceManager::registerDevice("JACK", std::shared_ptr<IDeviceFactory>(new JackDeviceFactory));
}

#ifdef JACK_PLUGIN
extern "C" AUD_PLUGIN_API void registerPlugin()
{
	JackDevice::registerPlugin();
}

extern "C" AUD_PLUGIN_API const char* getName()
{
	return "JACK";
}
#endif

AUD_NAMESPACE_END
