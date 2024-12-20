//
// TestCase.cpp
//


#include <stdexcept>
#include <cmath>
#include "CppUnit/TestCase.h"
#include "CppUnit/TestResult.h"
#include "CppUnit/estring.h"
#include <typeinfo>
#include <iostream>


using namespace std;


namespace CppUnit {


// Create a default TestResult
TestResult* TestCase::defaultResult()
{
	return new TestResult;
}


// Check for a failed general assertion
void TestCase::assertImplementation(bool condition, const std::string& conditionExpression, long lineNumber, const std::string& fileName)
{
	if (!condition)
		throw CppUnitException(conditionExpression, lineNumber, fileName);
}


void TestCase::loop1assertImplementation(bool condition, const std::string& conditionExpression, long lineNumber, long data1lineNumber, const std::string& fileName)
{
	if (!condition)
		throw CppUnitException(conditionExpression, lineNumber, data1lineNumber, fileName);
}


void TestCase::loop2assertImplementation(bool condition, const std::string& conditionExpression, long lineNumber, long data1lineNumber, long data2lineNumber, const std::string& fileName)
{
	if (!condition)
		throw CppUnitException(conditionExpression, lineNumber, data1lineNumber, data2lineNumber, fileName);
}


// Check for a failed equality assertion
void TestCase::assertEquals(double expected, double actual, double delta, long lineNumber, const std::string& fileName)
{
	if (fabs(expected - actual) > delta)
		assertImplementation(false, notEqualsMessage(expected, actual), lineNumber, fileName);
}


// Check for a failed equality assertion
void TestCase::assertEquals(const void* expected, const void* actual, long lineNumber, const std::string& fileName)
{
	if (expected != actual)
		assertImplementation(false, notEqualsMessage(expected, actual), lineNumber, fileName);
}


// Check for a failed equality assertion
void TestCase::assertEquals(const std::string& expected, const std::string& actual, long lineNumber, const std::string& fileName)
{
	if (expected != actual)
		assertImplementation(false, notEqualsMessage(expected, actual), lineNumber, fileName);
}


// Check for a failed equality assertion
void TestCase::assertEquals(const char* expected, const std::string& actual, long lineNumber, const std::string& fileName)
{
	if (std::string(expected) != actual)
		assertImplementation(false, notEqualsMessage(std::string(expected), actual), lineNumber, fileName);
}


void TestCase::assertNotNull(const void* pointer, const std::string& pointerExpression, long lineNumber, const std::string& fileName)
{
	if (pointer == nullptr)
		throw CppUnitException(pointerExpression + " must not be NULL", lineNumber, fileName);
}


void TestCase::assertNull(const void* pointer, const std::string& pointerExpression, long lineNumber, const std::string& fileName)
{
	if (pointer != nullptr)
		throw CppUnitException(pointerExpression + " must be NULL", lineNumber, fileName);
}


void TestCase::fail(const std::string& message, long lineNumber, const std::string& fileName)
{
	throw CppUnitException(std::string("fail: ") + message, lineNumber, fileName);
}


void TestCase::warn(const std::string& message, long lineNumber, const std::string& fileName)
{
	std::cout << "Warning [" << fileName << ':' << lineNumber << "]: " << message << std::endl;
}


// Run the test and catch any exceptions that are triggered by it
void TestCase::run(TestResult *result, const Test::Callback& callback)
{
	result->startTest(this);

	setUp();
	try
	{
		runTest();
	}
	catch (CppUnitException& e)
	{
		auto* copy = new CppUnitException(e);
		result->addFailure(this, copy);
	}
	catch (std::exception& e)
	{
		std::string msg(TestResult::demangle(typeid(e).name()));
		if (callback)
		{
			msg.append(": ").append(callback(e));
		}
		result->addError(this, new CppUnitException(msg));
	}
	catch (...)
	{
		auto* e = new CppUnitException ("unknown exception");
		result->addError (this, e);
	}
	tearDown ();
	result->endTest(this);
}


// A default run method
TestResult* TestCase::run()
{
	TestResult* result = defaultResult();

	run(result);
	return result;
}


// All the work for runTest is deferred to subclasses
void TestCase::runTest()
{
}


// Build a message about a failed equality check
std::string TestCase::notEqualsMessage(const void* expected, const void* actual)
{
	return "expected: " + estring(expected) + " but was: " + estring(actual);
}


// Build a message about a failed equality check
std::string TestCase::notEqualsMessage(const std::string& expected, const std::string& actual)
{
	return "expected: \"" + expected + "\" but was: \"" + actual + "\"";
}


} // namespace CppUnit
