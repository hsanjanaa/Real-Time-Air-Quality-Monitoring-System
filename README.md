# 🌫️ EcoWatch: Real-Time AQI Alert System

## Overview
EcoWatch is an STM32-based environmental monitoring system that measures air quality, temperature, and humidity in real time. Sensor data is transmitted via UART to a Python application for visualization and uploaded to ThingSpeak for cloud-based monitoring and analysis.

## Features
- Real-time AQI monitoring using MQ135
- Temperature and humidity sensing with DHT11
- UART communication between STM32 and Python
- Live data visualization using Python (PySerial + Matplotlib)
- Cloud data logging and remote monitoring via ThingSpeak
- LED and buzzer alerts for poor air quality
- Historical trend analysis through ThingSpeak dashboards

## Tech Stack
- STM32 NUCLEO-F446ZE
- Embedded C
- Python
- PySerial
- Matplotlib
- ThingSpeak
- UART Communication

## System Workflow
MQ135 & DHT11 Sensors → STM32 → UART → Python Dashboard → ThingSpeak Cloud

## Applications
- Smart Homes
- Indoor Air Quality Monitoring
- Laboratories
- Educational Projects
- Smart City Solutions

## Project Highlights
✔ Real-time sensor data acquisition  
✔ Python-based visualization dashboard  
✔ IoT cloud integration with ThingSpeak  
✔ Environmental monitoring and AQI alert system
