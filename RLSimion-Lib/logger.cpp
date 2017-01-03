#include "stdafx.h"
#include "logger.h"
#include "world.h"
#include "named-var-set.h"
#include "config.h"
#include "stats.h"
#include "timer.h"
#include "app.h"
#include "utils.h"
#include "SimGod.h"

void *CLogger::m_hLogFile= 0;

#define OUTPUT_LOG_XML_DESCRIPTOR_FILENAME "experiment-log.xml"
#define OUTPUT_LOG_FILENAME "experiment-log.bin"

#define HEADER_MAX_SIZE 16
#define EXPERIMENT_HEADER 1
#define EPISODE_HEADER 2
#define STEP_HEADER 3
#define EPISODE_END_HEADER 4

//we pack every int/double as 64bit data to avoid struct-padding issues (the size of the struct might not be the same in C++ and C#

struct ExperimentHeader
{
	__int64 magicNumber = EXPERIMENT_HEADER;
	__int64 fileVersion = CLogger::BIN_FILE_VERSION;
	__int64 numEpisodes = 0;

	__int64 padding[HEADER_MAX_SIZE - 3]; //extra space
	ExperimentHeader()
	{
		memset(padding, 0, sizeof(padding));
	}
};

struct EpisodeHeader
{
	__int64 magicNumber = EPISODE_HEADER;
	__int64 episodeType;
	__int64 episodeIndex;
	__int64 numVariablesLogged;

	__int64 padding[HEADER_MAX_SIZE - 4]; //extra space
	EpisodeHeader()
	{
		memset(padding, 0, sizeof(padding));
	}
};

struct StepHeader
{
	__int64 magicNumber = STEP_HEADER;
	__int64 stepIndex;

	double experimentRealTime;
	double episodeSimTime;
	double episodeRealTime;

	__int64 padding[HEADER_MAX_SIZE - 5]; //extra space
	StepHeader()
	{
		memset(padding, 0, sizeof(padding));
	}
};

CLogger::CLogger(CConfigNode* pConfigNode)
{
	if (!pConfigNode) return;

	m_bLogEvaluationEpisodes= BOOL_PARAM(pConfigNode,"Log-eval-episodes", "Log evaluation episodes?",true);

	m_bLogTrainingEpisodes= BOOL_PARAM(pConfigNode,"Log-training-episodes", "Log training episodes?",false);

	m_logFreq= DOUBLE_PARAM(pConfigNode,"Log-Freq","Log frequency. Simulation time in seconds.",0.25);


	m_pEpisodeTimer = new CTimer();
	m_pExperimentTimer = new CTimer();
	m_lastLogSimulationT = 0.0;
}



void CLogger::setLogDirectory(const char* xmlFilePath)
{
	if (!xmlFilePath)
		throw(std::exception("CLogger. No output directory provided."));

	strcpy_s(m_outputDir, MAX_FILENAME_LENGTH, xmlFilePath);
	
	//we register the name for input/output stuff
	char fullLogFilename[MAX_FILENAME_LENGTH];
	sprintf_s(fullLogFilename, MAX_FILENAME_LENGTH, "%s/%s", m_outputDir, OUTPUT_LOG_XML_DESCRIPTOR_FILENAME);
	CSimGod::registerOutputFile(fullLogFilename);
	sprintf_s(fullLogFilename, MAX_FILENAME_LENGTH, "%s/%s", m_outputDir, OUTPUT_LOG_FILENAME);
	CSimGod::registerOutputFile(fullLogFilename);

	//open the log file
	m_hLogFile = openLogFile(fullLogFilename);
}


CLogger::~CLogger()
{
	if (m_pExperimentTimer) delete m_pExperimentTimer;
	if (m_pEpisodeTimer) delete m_pEpisodeTimer;

	closeOutputPipe();

	for (auto it = m_stats.begin(); it != m_stats.end(); it++)
		delete *it;

	closeLogFile(m_hLogFile);
}


bool CLogger::isEpisodeTypeLogged(bool evalEpisode)
{
	return (evalEpisode && m_bLogEvaluationEpisodes.get()) || (!evalEpisode && m_bLogTrainingEpisodes.get());
}



void CLogger::writeLogFileXMLDescriptor(const char* filename)
{
	char buffer[BUFFER_SIZE];

	void * logXMLDescriptorFile= openLogFile(filename);
	sprintf_s(buffer,BUFFER_SIZE, "<ExperimentLogDescriptor BinaryDataFile=\"%s\">\n",OUTPUT_LOG_FILENAME);
	writeEpisodeTypesToBuffer(buffer);
	writeNamedVarSetDescriptorToBuffer(buffer, "State", CSimionApp::get()->pWorld->getDynamicModel()->getStateDescriptorPtr()); //state
	writeNamedVarSetDescriptorToBuffer(buffer, "Action", CSimionApp::get()->pWorld->getDynamicModel()->getActionDescriptorPtr()); //action
	writeNamedVarSetDescriptorToBuffer(buffer, "Reward", CSimionApp::get()->pWorld->getRewardVector()->getPropertiesPtr());
	writeStatDescriptorToBuffer(buffer);
	strcat_s(buffer, BUFFER_SIZE, "</ExperimentLogDescriptor>");
	writeLogBuffer(logXMLDescriptorFile, buffer, strlen(buffer));
	
	closeLogFile(logXMLDescriptorFile);
}

void CLogger::writeEpisodeTypesToBuffer(char* pOutBuffer)
{
	if (m_bLogEvaluationEpisodes.get()) strcat_s(pOutBuffer, BUFFER_SIZE
		, "  <Episode-Type Id=\"0\">Evaluation</Episode-Type>\n");
	if (m_bLogTrainingEpisodes.get()) strcat_s(pOutBuffer, BUFFER_SIZE
		, "  <Episode-Type Id=\"0\">Training</Episode-Type>\n");
}

void CLogger::writeStatDescriptorToBuffer(char* pOutBuffer)
{
	char buffer[BUFFER_SIZE];

	for (auto iterator = m_stats.begin(); iterator != m_stats.end(); iterator++)
	{
		sprintf_s(buffer, BUFFER_SIZE, "  <Stat-variable>%s/%s</Stat-variable>\n", (*iterator)->getKey(),(*iterator)->getSubkey());
		strcat_s(pOutBuffer, BUFFER_SIZE, buffer);
	}
}
void CLogger::writeNamedVarSetDescriptorToBuffer(char* pOutBuffer, const char* id, const CDescriptor* descriptor)
{
	char buffer[BUFFER_SIZE];
	for (int i = 0; i < descriptor->size(); i++)
	{
		sprintf_s(buffer, BUFFER_SIZE, "  <%s-variable>%s</%s-variable>\n", id, (*descriptor)[i].getName(), id);
		strcat_s(pOutBuffer, BUFFER_SIZE, buffer);
	}
}

void CLogger::firstEpisode()
{
	//set episode start time
	m_pEpisodeTimer->startTimer();

	char fullLogFilename[MAX_FILENAME_LENGTH];

	//generate the xml descriptor of the log file
	sprintf_s(fullLogFilename, MAX_FILENAME_LENGTH, "%s/%s", m_outputDir, OUTPUT_LOG_XML_DESCRIPTOR_FILENAME);
	writeLogFileXMLDescriptor(fullLogFilename);

	//write the log file header
	writeExperimentHeader();
}

void CLogger::lastEpisode()
{
	//writeLogBuffer(Output::LogFile, "</Experiment>\n");
}

void CLogger::firstStep()
{
	//initialise the episode reward
	m_episodeRewardSum = 0.0;

	//set episode start time
	m_pEpisodeTimer->startTimer();

	m_lastLogSimulationT = 0.0;

	bool bEvalEpisode = CSimionApp::get()->pExperiment->isEvaluationEpisode();

	//reset stats
	for (auto it = m_stats.begin(); it != m_stats.end(); it++)
		(*it)->reset();

	if (isEpisodeTypeLogged(bEvalEpisode))
		writeEpisodeHeader();
}

void CLogger::lastStep()
{
	bool bEvalEpisode = CSimionApp::get()->pExperiment->isEvaluationEpisode();
	if (!isEpisodeTypeLogged(bEvalEpisode)) return;

	//log the end of the episode: this way we don't have to precalculate the number of steps logged per episode
	writeEpisodeEndHeader();

	//in case this is the last step of an evaluation episode, we log it and send the info to the host if there is one
	char buffer[BUFFER_SIZE];
	int episodeIndex = CSimionApp::get()->pExperiment->getEvaluationEpisodeIndex();
	int numEpisodes = CSimionApp::get()->pExperiment->getNumEvaluationEpisodes();

	//log the progress if an evaluation episode has ended
	if (CSimionApp::get()->pExperiment->isEvaluationEpisode() && numEpisodes>0)
	{
		sprintf_s(buffer, BUFFER_SIZE, "%f,%f", (double)(episodeIndex - 1) / (std::max(1.0, (double)numEpisodes - 1))
			, m_episodeRewardSum / (double)CSimionApp::get()->pExperiment->getStep());
		logMessage(MessageType::Evaluation, buffer);
	}
}

void CLogger::timestep(CState* s, CAction* a, CState* s_p, CReward* r)
{
	bool bEvalEpisode = CSimionApp::get()->pExperiment->isEvaluationEpisode();
	//we add the scalar reward in evaluation episodes for monitoring purposes, no matter if we are logging this type of episode or not
	if (bEvalEpisode) m_episodeRewardSum += r->getSumValue();

	if (!isEpisodeTypeLogged(bEvalEpisode)) return;

	//update experiment stats
	for (auto iterator = m_stats.begin(); iterator != m_stats.end(); iterator++)
	{
		(*iterator)->addExperimentSample();
	}

	//output episode log data
	if (CSimionApp::get()->pWorld->getStepStartT() - m_lastLogSimulationT >= m_logFreq.get()
		|| CSimionApp::get()->pExperiment->isFirstStep() || CSimionApp::get()->pExperiment->isLastStep())
	{
		writeStepData(s, a, s_p, r);
		m_lastLogSimulationT = CSimionApp::get()->pWorld->getStepStartT();
	}
}

void CLogger::writeStepData(CState* s, CAction* a, CState* s_p, CReward* r)
{
	int offset = 0;
	char buffer[BUFFER_SIZE];
	buffer[0] = 0;

	offset += writeStepHeaderToBuffer(buffer, offset);
	
	offset += writeNamedVarSetToBuffer(buffer, offset, s);
	offset += writeNamedVarSetToBuffer(buffer, offset, a);
	offset += writeNamedVarSetToBuffer(buffer, offset, r);
	offset += writeStatsToBuffer(buffer, offset);

	writeLogBuffer(m_hLogFile, buffer, offset);
}

void CLogger::writeExperimentHeader()
{
	ExperimentHeader header;

	if (m_bLogEvaluationEpisodes.get()) header.numEpisodes += CSimionApp::get()->pExperiment->getNumEvaluationEpisodes();
	if (m_bLogTrainingEpisodes.get()) header.numEpisodes += CSimionApp::get()->pExperiment->getNumTrainingEpisodes();

	writeLogBuffer(m_hLogFile, (char*) &header, sizeof(ExperimentHeader));
}

void CLogger::writeEpisodeHeader()
{
	EpisodeHeader header;

	header.episodeIndex = CSimionApp::get()->pExperiment->getRelativeEpisodeIndex();
	header.episodeType = (CSimionApp::get()->pExperiment->isEvaluationEpisode() ? 0 : 1);
	header.numVariablesLogged =
		CSimionApp::get()->pWorld->getDynamicModel()->getActionDescriptor().size()
		+ CSimionApp::get()->pWorld->getDynamicModel()->getStateDescriptor().size()
		+ CSimionApp::get()->pWorld->getRewardVector()->getNumVars()
		+ m_stats.size();

	writeLogBuffer(m_hLogFile, (char*) &header, sizeof(EpisodeHeader));
}

void CLogger::writeEpisodeEndHeader()
{
	StepHeader episodeEndHeader;
	memset(&episodeEndHeader, 0, sizeof(StepHeader));
	episodeEndHeader.magicNumber = EPISODE_END_HEADER;
	writeLogBuffer(m_hLogFile, (char*)&episodeEndHeader, sizeof(StepHeader));
}

int CLogger::writeStepHeaderToBuffer(char* buffer, int offset)
{
	StepHeader header;
	header.stepIndex = CSimionApp::get()->pExperiment->getStep();
	header.episodeRealTime = m_pEpisodeTimer->getElapsedTime();
	header.episodeSimTime = CSimionApp::get()->pWorld->getT();
	header.experimentRealTime = m_pExperimentTimer->getElapsedTime();

	memcpy_s(buffer + offset, BUFFER_SIZE, (char*)&header, sizeof(StepHeader));

	return sizeof(header);
}

int CLogger::writeNamedVarSetToBuffer(char* buffer, int offset, const CNamedVarSet* pNamedVarSet)
{
	int numVars = pNamedVarSet->getNumVars();
	double* pDoubleBuffer = (double*)(buffer + offset);
	for (int i = 0; i < numVars; ++i)
		pDoubleBuffer[i] = pNamedVarSet->getValue(i);
	return numVars* sizeof(double);
}

int CLogger::writeStatsToBuffer(char* buffer, int offset)
{
	int numVars = m_stats.size();
	double* pDoubleBuffer = (double*)(buffer + offset);
	int i = 0;
	for (auto it = m_stats.begin(); it != m_stats.end(); ++it)
	{
		pDoubleBuffer[i] = (*it)->getValue();
		++i;
	}
	return numVars* sizeof(double);
}


void CLogger::addVarToStats(const char* key, const char* subkey, double* variable)
{
	//all stats added by the loaded classes are calculated
	//if (m_pParameters->getChild(key))
	m_stats.push_back(new CStats(key, subkey, (void*) variable, Double));
}

void CLogger::addVarToStats(const char* key, const char* subkey, int* variable)
{
	//all stats added by the loaded classes are calculated
	//if (m_pParameters->getChild(key))
	m_stats.push_back(new CStats(key, subkey, (void*) variable, Int));
}

void CLogger::addVarToStats(const char* key, const char* subkey, unsigned int* variable)
{
	//all stats added by the loaded classes are calculated
	//if (m_pParameters->getChild(key))
	m_stats.push_back(new CStats(key, subkey, (void*)variable, UnsignedInt));
}

void CLogger::addVarSetToStats(const char* key, CNamedVarSet* varset)
{
	for (int i = 0; i < varset->getNumVars(); i++)
	{
		m_stats.push_back(new CStats(key, varset->getProperties()[i].getName(), varset->getValuePtr(i), Double));
	}
}

//WINDOWS-SPECIFIC STUFF
#include <windows.h>
#include <string>

void* CLogger::openLogFile(const char* logFilename)
{
	size_t convertedChars;

	wchar_t w_filename[BUFFER_SIZE];

	mbstowcs_s(&convertedChars, w_filename, BUFFER_SIZE, logFilename, BUFFER_SIZE);

	return CreateFile(w_filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0,0);
}
void CLogger::closeLogFile(void* fileHandle)
{
	if (fileHandle) CloseHandle(fileHandle);
	else logMessage(MessageType::Warning, "Could not close log file because it wasn't opened.");
}

void CLogger::writeLogBuffer(void* fileHandle, const char* pBuffer, int numBytes)
{
	unsigned long numBytesWritten = 0;
	if (fileHandle)
		WriteFile(fileHandle, pBuffer, numBytes, &numBytesWritten,0);
}

MessageOutputMode CLogger::m_messageOutputMode = MessageOutputMode::Console;
void* CLogger::m_outputPipe = 0;

void CLogger::logMessage(MessageType type, const char* message)
{
	char messageLine[1024];

	if (m_messageOutputMode == MessageOutputMode::NamedPipe && m_outputPipe)
	{
		switch (type)
		{
		case Warning:
			sprintf_s(messageLine, 1024, "<Message>WARNING: %s</Message>", message); break;
		case Progress:
			sprintf_s(messageLine, 1024, "<Progress>%s</Progress>", message); break;
		case Evaluation:
			sprintf_s(messageLine, 1024, "<Evaluation>%s</Evaluation>", message); break;
		case Info:
			sprintf_s(messageLine, 1024, "<Message>%s</Message>", message); break;
		case Error:
			sprintf_s(messageLine, 1024, "<Error>ERROR: %s</Error>", message); break;
		}
		writeLogBuffer(m_outputPipe, messageLine, strlen(messageLine));
	}
	else
	{
		switch (type)
		{
		case Warning:
			printf("WARNING: %s\n", message); break;
		case Progress:
			//extra spaces to avoid overwriting only partially previous message
			printf("PROGRESS: %s                     \r", message); break;
		case Evaluation:
			//extra spaces to avoid overwriting only partially previous message
			printf("EVALUATION: %s\n", message); break;
		case Info:
			printf("%s\n", message); break;
		case Error:
			printf("ERROR: %s\n",message); break;
		}
	}
}

void CLogger::closeOutputPipe()
{
	if (m_outputPipe)
	{
		FlushFileBuffers(m_outputPipe);
		CloseHandle(m_outputPipe);
		m_outputPipe = 0;
	}
}

void CLogger::createOutputPipe(const char* pipeName)
{
	size_t convertedChars;
	
	wchar_t w_pipename[512];
	wchar_t w_completePipename[512] = L"\\\\.\\pipe\\";

	
	mbstowcs_s(&convertedChars, w_pipename,512, pipeName, 512);
	wcscat_s(w_completePipename, w_pipename);
	
	m_outputPipe = CreateFile(
		w_completePipename,   // pipe name 
		GENERIC_WRITE,
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,  // opens existing pipe 
		0,              // default attributes 
		NULL);          // no template file 

	if (m_outputPipe == INVALID_HANDLE_VALUE)
	{
		//printf("FAILED\n");
		logMessage(MessageType::Error, "Could not create pipe");
		return;
	}
	//printf("OK\n");

	m_messageOutputMode = MessageOutputMode::NamedPipe;
	logMessage(MessageType::Info, "Pipe succesfully created");
}