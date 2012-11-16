################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/WhiteMagiC+2.cpp 

OBJS += \
./src/WhiteMagiC+2.o 

CPP_DEPS += \
./src/WhiteMagiC+2.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -I/home/stephan/workspace/org.eclipse.paho.mqtt.c/src/linux_ia32 -I/home/stephan/workspace/org.eclipse.paho.mqtt.c/src/linux_ia64 -I/usr/local/include -O0 -g3 -Wall -c -fmessage-length=0 -fPIC -std=c++0x -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


