#pragma once
#include "parameterized-object.h"

class CLinearVFA;
class CGaussianNoise;

class CNamedVarSet;
typedef CNamedVarSet CState;
typedef CNamedVarSet CAction;

class tinyxml2::XMLElement;

class CSingleOutputVFAPolicy : public CParamObject
{

protected:
	CLinearVFA *m_pVFA;
	CGaussianNoise *m_pExpNoise;
	int m_outputActionIndex;
	const char* m_outputAction;
public:
	CSingleOutputVFAPolicy(tinyxml2::XMLElement* pParameters);
	~CSingleOutputVFAPolicy();

	void selectAction(CState *s, CAction *a);

	CLinearVFA* getVFA(){ return m_pVFA; }
	CGaussianNoise* getExpNoise(){ return m_pExpNoise; }
	const char* getOutputAction(){ return m_outputAction; }
	int getOutputActionIndex(){ return m_outputActionIndex; }
};