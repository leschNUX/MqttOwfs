#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include "MqttOwfs.h"
#include "Plateforms.h"
#include "SimpleFolders.h"


using namespace std;


MqttOwfs::MqttOwfs() : MqttDaemon("owfs", "MqttOwfs"), m_RefreshDevicesInterval(90), m_OwfsClient()
{
	m_OwfsClient.Initialisation("127.0.0.1", 4304);
}

MqttOwfs::~MqttOwfs()
{
    m_OwDevices.clear();
}

void MqttOwfs::DaemonConfigure(SimpleIni& iniFile)
{
	string svalue;
	int ivalue;
	char chvalue;

	svalue = iniFile.GetValue("owfs", "server", "127.0.0.1");
	ivalue = iniFile.GetValue("owfs", "port", 4304);
	m_OwfsClient.Initialisation(svalue, ivalue);
	LOG_VERBOSE(m_Log) << "Connect to owserver " << svalue << ":" << ivalue;

	ivalue = iniFile.GetValue("owfs", "devicesinterval", 90);
	m_RefreshDevicesInterval = ivalue;
	LOG_VERBOSE(m_Log) << "Set RefreshDevicesInterval to " << m_RefreshDevicesInterval;

	ivalue = iniFile.GetValue("owfs", "valuesinterval", 10);
	owDevice::SetDefaultRefreshInterval(ivalue);
	LOG_VERBOSE(m_Log) << "Set RefreshValuesInterval to " << ivalue;

	svalue = iniFile.GetValue("owfs", "temperaturescale", "C");
	chvalue = toupper(svalue.front());

	switch (chvalue)
	{
		case 'C':
			m_OwfsClient.SetTemperatureScale(owfscpp::Centigrade);
			LOG_VERBOSE(m_Log) << "Set TemperatureScale to Centigrade";
			break;
		case 'F':
			m_OwfsClient.SetTemperatureScale(owfscpp::Fahrenheit);
			LOG_VERBOSE(m_Log) << "Set TemperatureScale to Fahrenheit";
			break;
		case 'K':
			m_OwfsClient.SetTemperatureScale(owfscpp::Kelvin);
			LOG_VERBOSE(m_Log) << "Set TemperatureScale to Kelvin";
			break;
		case 'R':
			m_OwfsClient.SetTemperatureScale(owfscpp::Rankine);
			LOG_VERBOSE(m_Log) << "Set TemperatureScale to Rankine";
			break;
		default:
			LOG_WARNING(m_Log) << "Unknown TemperatureScale " << chvalue << " (Possible values C,F,K,R)";
	}

	svalue = iniFile.GetValue("owfs", "pressurescale", "Mbar");
	std::transform(svalue.begin(), svalue.end(), svalue.begin(), ::toupper);
	if (svalue == "MBAR")
	{
		m_OwfsClient.SetPressureScale(owfscpp::Mbar);
		LOG_VERBOSE(m_Log) << "Set PressureScale to Mbar";
	}
	else if (svalue == "ATM")
	{
		m_OwfsClient.SetPressureScale(owfscpp::Atm);
		LOG_VERBOSE(m_Log) << "Set PressureScale to Atm";
	}
	else if (svalue == "MMHG")
	{
		m_OwfsClient.SetPressureScale(owfscpp::MmHg);
		LOG_VERBOSE(m_Log) << "Set PressureScale to MmHg";
	}
	else if (svalue == "INHG")
	{
		m_OwfsClient.SetPressureScale(owfscpp::InHg);
		LOG_VERBOSE(m_Log) << "Set PressureScale to InHg";
	}
	else if (svalue == "PSI")
	{
		m_OwfsClient.SetPressureScale(owfscpp::Psi);
		LOG_VERBOSE(m_Log) << "Set PressureScale to Psi";
	}
	else if (svalue == "PA")
	{
		m_OwfsClient.SetPressureScale(owfscpp::Pa);
		LOG_VERBOSE(m_Log) << "Set PressureScale to Pa";
	}
	else
	{
		LOG_WARNING(m_Log) << "Unknown PressureScale " << svalue << " (Possible values Mbar,Atm,MmHg,InHg,Psi,Pa)";
	}

	svalue = iniFile.GetValue("owfs", "uncachedread", "FALSE");
	std::transform(svalue.begin(), svalue.end(), svalue.begin(), ::toupper);
	if ((svalue == "1") || (svalue == "TRUE") || (svalue == "YES"))
	{
	    owDevice::SetDefaultUncachedRead(true);
		LOG_VERBOSE(m_Log) << "Set Uncached read by default";
	}

	m_OwDevices.clear();
	DevicesConfigure(iniFile);
}

void MqttOwfs::DevicesConfigure(SimpleIni& iniFile)
{
	int ivalue;
	string svalue;
	string configName;
	string displayName;
	size_t pos;
	int round;

	for (SimpleIni::SectionIterator itSection = iniFile.beginSection(); itSection != iniFile.endSection(); ++itSection)
	{
		if ((*itSection) == "owfs") continue;
		if ((*itSection) == "mqtt") continue;
		if ((*itSection) == "log") continue;

		configName = (*itSection);
		pos = configName.find("/");
		if (pos == string::npos)
		{
			LOG_ERROR(m_Log) << "Device " << configName << " is improperly formatted, it must contain a '/'.";
			continue;
		}

		displayName = iniFile.GetValue(configName, "displayname", "");
		if (displayName == "")
		{
			LOG_ERROR(m_Log) << "Device " << configName << " have not display name";
			continue;
		}

		round = iniFile.GetValue(configName, "round", -1);

		OwDeviceAdd(displayName, configName, round);

		svalue = iniFile.GetValue(configName, "uncachedread", "");
		if (svalue != "")
		{
			std::transform(svalue.begin(), svalue.end(), svalue.begin(), ::toupper);
			if ((svalue == "1") || (svalue == "TRUE") || (svalue == "YES"))
            {
				m_OwDevices[configName].SetUncachedRead(true);
                LOG_VERBOSE(m_Log) << "Set special uncached read on this device to true.";
            }
			else if ((svalue == "0") || (svalue == "FALSE") || (svalue == "NO"))
            {
				m_OwDevices[configName].SetUncachedRead(false);
                LOG_VERBOSE(m_Log) << "Set special uncached read on this device to false.";
            }
			else
				LOG_WARNING(m_Log) << "Device " << configName << " have an understanding uncachedread value.";
		}

        ivalue = iniFile.GetValue(configName, "refreshinterval", 0);
        if (ivalue > 0)
        {
            m_OwDevices[configName].SetRefreshInterval(ivalue);
            LOG_VERBOSE(m_Log) << "Set special refresh interval on this device : " << ivalue << " s.";
        }
	}
}

void MqttOwfs::RefreshDevices(bool forceRefresh)
{
    list<string> lstDir;
    list<string>::iterator iDir;
    string device;
    time_t timeNow = time((time_t*)0);
    static time_t lastRefreshDevices = timeNow;


	if((timeNow-lastRefreshDevices<m_RefreshDevicesInterval)&&(!forceRefresh)) return;
    lastRefreshDevices = timeNow;

    try
    {
        lstDir = m_OwfsClient.DirAll("/");
    }
    catch (const exception& e)
    {
        LOG_ERROR(m_Log) << "Unable to find owdevices : " << e.what();
    }

    for(iDir = lstDir.begin(); iDir != lstDir.end(); ++iDir)
    {
        if(iDir->substr(0,1)=="/")
            device = iDir->substr(1);
        else
            device = iDir->substr(0);

        if(!OwDeviceExist(device))
            OwDeviceAdd(device);
    }
}


bool MqttOwfs::OwDeviceExist(const string& device)
{
    std::map<std::string, owDevice>::iterator it;
    size_t length;


    length = device.length();
    for(it=m_OwDevices.begin(); it!=m_OwDevices.end(); ++it)
    {
        if(it->first.compare(0, length, device)==0)
            return true;
    }

    return false;
}

string MqttOwfs::OwGetValue(const string& configName, int round, bool uncachedRead)
{
    string svalue;
    double dvalue;
    ostringstream s;


    try
    {
		m_OwfsClient.SetOwserverFlag(owfscpp::Uncached, uncachedRead);
        svalue = m_OwfsClient.Get(configName);
    }
    catch (const exception& e)
    {
        LOG_WARNING(m_Log) << "Unable to read " << configName << " : " << e.what();
        return "";
    }

    if(round<0) return svalue;
    dvalue = atof(svalue.c_str());
    s << fixed << setprecision(round) << dvalue;

    return s.str();
}

string MqttOwfs::OwIsPresent(const string& configName, bool uncachedRead)
{
    list<string> lstDir;
    list<string>::iterator itPos;
    list<string>::iterator itEnd;
    string isPresent = "0";


    LOG_TRACE(m_Log) << "Search presence of " << configName << " with uncachedread to " << uncachedRead;
    try
    {
		m_OwfsClient.SetOwserverFlag(owfscpp::Uncached, uncachedRead);
        lstDir = m_OwfsClient.DirAll("");

        itEnd = lstDir.end();
        itPos = find(lstDir.begin() , itEnd, "/"+configName);
        if(itPos != itEnd) isPresent = "1";
    }
    catch (const exception& e)
    {
        LOG_WARNING(m_Log) << "Unable to get presence of " << configName << " : " << e.what();
    }

    LOG_TRACE(m_Log) << "Found " << isPresent;
    return isPresent;
}

void MqttOwfs::OwDeviceAdd(const string& displayName, const string& configName, int round)
{
    LOG_VERBOSE(m_Log) << "Device created " << displayName <<" : "<< configName << " (round " << round << ")";
    m_OwDevices.emplace(piecewise_construct, forward_as_tuple(configName), forward_as_tuple(displayName, configName, round));
    RefreshValue(m_OwDevices[configName]);
}

void MqttOwfs::OwDeviceAdd(const string& name)
{
	string svalue;
	int family;


	try
	{
		svalue = m_OwfsClient.Get(name + "/family");
	}
	catch (const exception& e)
	{
		LOG_WARNING(m_Log) << "Unable to read family of device " << name << " : " << e.what();
		return;
	}

	istringstream iss(svalue);
	iss >> hex >> family;

    switch(family)
    {
		case 0x01 : 	//DS2401
		    OwDeviceAdd(name, name+"/IsPresent", -1);
			break;
		case 0x05 : 	//DS2405
		    OwDeviceAdd(name, name+"/PIO", -1);
			break;
		case 0x10 :		//DS18S20, DS1920
		    OwDeviceAdd(name, name+"/temperature9", 1);
			break;
		case 0x12 :		//DS2406/07
		    OwDeviceAdd(name, name+"/PIO.A", -1);
			break;
		case 0x1D :		//DS2423
		    OwDeviceAdd(name, name+"/counters.A", -1);
			break;
		case 0x20 : 	//DS2450
		    OwDeviceAdd(name, name+"/PIO.A", -1);
			break;
		case 0x21 :		//DS1921
		    OwDeviceAdd(name, name+"/temperature9", 1);
			break;
		case 0x22 :		//DS1822
		    OwDeviceAdd(name, name+"/temperature9", 1);
			break;
		case 0x26 :		//DS2438
		    OwDeviceAdd(name, name+"/VDD", 1);
			break;
		case 0x28 : 	//DS18B20
		    OwDeviceAdd(name, name+"/temperature9", 1);
			break;
		case 0x29 : 	//DS2408
		    OwDeviceAdd(name, name+"/PIO.BYTE", -1);
			break;
		case 0x3A : 	//DS2413
		    OwDeviceAdd(name, name+"/PIO.A", -1);
			break;
    }

    LOG_VERBOSE(m_Log) << "Device found " << name;
}

bool MqttOwfs::RefreshValue(owDevice& device)
{
    string value;
    string name = device.GetDeviceName();

    if(device.OnlyPresence())
    {
        LOG_TRACE(m_Log) << "Search device " << name;
        value = OwIsPresent(name, device.GetUncachedRead());
    }
    else
    {
        LOG_TRACE(m_Log) << "Read value for " << name;
        value = OwGetValue(name, device.GetRound(), device.GetUncachedRead());
    }

    device.IsRefreshed();
    if(value==device.GetValue()) return false;

    device.SetValue(value);
	lock_guard<mutex> lock(m_MqttQueueAccess);
	m_MqttQueue.emplace(device.GetDisplayName(), value);
	LOG_VERBOSE(m_Log) << "New value for " << name << " : " << value;

	return true;
}

void MqttOwfs::RefreshValues(bool forceRefresh)
{
    std::map<std::string, owDevice>::iterator it;
	bool newvalue = false;

    for(it=m_OwDevices.begin(); it!=m_OwDevices.end(); ++it)
    {
        if((forceRefresh)||(it->second.RefreshNeeded()))
        {
            if(RefreshValue(it->second)) newvalue = true;
        }
    }

	if(newvalue) m_MqttQueueCond.notify_one();
}

void MqttOwfs::MessageForService(const string& msg)
{
	if (msg == "REQUEST")
	{
		lock_guard<mutex> lock(m_MqttQueueAccess);
		for (map<string, owDevice>::iterator it = m_OwDevices.begin(); it != m_OwDevices.end(); ++it)
			m_MqttQueue.emplace(it->second.GetDisplayName(), it->second.GetValue());

		m_MqttQueueCond.notify_one();
	}
	else if (msg == "REFRESH_DEVICES")
	{
		RefreshDevices(true);
	}
	else if (msg == "REFRESH_VALUES")
	{
		RefreshValues(true);
	}
	else if (msg == "RELOAD_CONFIG")
	{
	    m_RestartCond.notify_one();
	}
	else
	{
		LOG_WARNING(m_Log) << "Unknown command for service " << msg;
	}
}

void MqttOwfs::MessageForDevice(const string& device, const string& msg)
{
	map<string, owDevice>::iterator it;

	for (it = m_OwDevices.begin(); it != m_OwDevices.end(); ++it)
	{
		if (it->second.GetDisplayName() == device) break;
	}

	if (it == m_OwDevices.end())
	{
		LOG_WARNING(m_Log) << "Unknown device " << device;
		return;
	}

	if (msg == "REQUEST")
	{
		lock_guard<mutex> lock(m_MqttQueueAccess);
		m_MqttQueue.emplace(it->second.GetDisplayName(), it->second.GetValue());
		m_MqttQueueCond.notify_one();
		return;
	}

	if (msg.length() > 20)
	{
		LOG_WARNING(m_Log) << "Message seem to be very long, truncate to 20 characters.";
		m_OwfsClient.Write(it->first, msg.substr(0, 20));
	}
	else
	{
		m_OwfsClient.Write(it->first, msg);
	}

	if(RefreshValue(it->second))
		m_MqttQueueCond.notify_one();
}

void MqttOwfs::on_message(const string& topic, const string& message)
{
	LOG_VERBOSE(m_Log) << "Mqtt receive " << topic << " : " << message;

	string mainTopic = GetMainTopic();
	if (topic.substr(0, mainTopic.length()) != mainTopic)
	{
		LOG_WARNING(m_Log) << "Not for me (" << mainTopic << ")";
		return;
	}

	if (topic.substr(mainTopic.length(), 7) != "command")
	{
		LOG_WARNING(m_Log) << "Not a command (waiting " << mainTopic+"command" << ")";
		return;
	}

	if (topic.length() == mainTopic.length() + 7) return MessageForService(message);

	string device = topic.substr(mainTopic.length() + 8);
	return MessageForDevice(device, message);
}

int MqttOwfs::DaemonLoop(int argc, char* argv[])
{
    int ret = 0;
	LOG_ENTER;
	RefreshDevices(true);

	Subscribe(GetMainTopic() + "command/#");
	LOG_VERBOSE(m_Log) << "Subscript to : " << GetMainTopic() + "command/#";

	SendMqttMessages();

	bool bStop = false;
	bool bPause = false;
	while (!bStop)
	{
		int cond = Service::Get()->WaitFor({ m_RestartCond, m_MqttQueueCond }, 250);
		switch(cond)
		{
		    case 0 :
                switch (Service::Get()->GetStatus())
                {
                    case Service::StatusKind::PAUSE:
                        bPause = true;
                        break;
                    case Service::StatusKind::START:
                        bPause = false;
                        cond = 1;
                        break;
                    case Service::StatusKind::STOP:
                        bStop = true;
                        break;
                }
                break;
            case 1 :
                bStop = true;
                ret = MqttDaemon::RESTART_MQTTDAEMON;
                break;
		}
		if (!bPause)
		{
            RefreshDevices(false);
            RefreshValues(false);
			SendMqttMessages();
		}
	}

	LOG_EXIT_OK;
    return ret;
}

void MqttOwfs::SendMqttMessages()
{
	lock_guard<mutex> lock(m_MqttQueueAccess);
	while (!m_MqttQueue.empty())
	{
		MqttQueue& mqttQueue = m_MqttQueue.front();
		LOG_VERBOSE(m_Log) << "Send " << mqttQueue.Topic << " : " << mqttQueue.Message;
		Publish(mqttQueue.Topic, mqttQueue.Message);
		m_MqttQueue.pop();
	}
}
