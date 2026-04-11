#pragma once

class Application {
public:
	Application();
	~Application();

	void run();
private:
	void init();
	void shutdown();
};