#pragma once

#include <vector>
using namespace std;
class NamedVarSet;
using State = NamedVarSet;
using Action = NamedVarSet;

class DrawableFunction
{
public:

	virtual unsigned int getNumOutputs() = 0;
	virtual const vector<double>& evaluate(const State* s, const Action* a) = 0;
	virtual const vector<string>& getInputStateVariables() = 0;
	virtual const vector<string>& getInputActionVariables() = 0;
	
};
