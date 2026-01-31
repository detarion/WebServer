#!/bin/bash

# Переменные для настройки
SERVICE_NAME="webserver" # Имя вашего демона
REMOTE_USER="root" # Имя пользователя на удаленном сервере
REMOTE_HOST="45.82.152.163"  # IP-адрес или hostname сервера
REMOTE_SSH_PORT="22" # Порт SSH, если не стандартный
LOCAL_FILE_PATH="/home/user/Projects/WebServer/source/${SERVICE_NAME}" # Путь к файлу на вашем локальном компьютере
REMOTE_FILE_PATH="/usr/local/bin/" # Путь к файлу на удаленном сервере

echo "Компиляция ${SERVICE_NAME}..."
g++ ${SERVICE_NAME}.cpp -o ${SERVICE_NAME}
if [ $? -ne 0 ]; then
    echo "Ошибка: Компиляция проекта"
    exit 1
fi

# 1. Остановка демона
echo "Останавливаем демон ${SERVICE_NAME}..."
ssh -p ${REMOTE_SSH_PORT} ${REMOTE_USER}@${REMOTE_HOST} "sudo systemctl stop ${SERVICE_NAME}"
if [ $? -ne 0 ]; then
    echo "Ошибка: Не удалось остановить демон. Прерывание."
    exit 1
fi

# 2. Копирование файла
echo "Копируем файл ${LOCAL_FILE_PATH} на ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_FILE_PATH}..."
scp -P ${REMOTE_SSH_PORT} ${LOCAL_FILE_PATH} ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_FILE_PATH}
if [ $? -ne 0 ]; then
    echo "Ошибка: Не удалось скопировать файл. Прерывание."
    exit 1
fi

# 3. Запуск демона
echo "Запускаем демон ${SERVICE_NAME}..."
ssh -p ${REMOTE_SSH_PORT} ${REMOTE_USER}@${REMOTE_HOST} "sudo systemctl start ${SERVICE_NAME}"
if [ $? -ne 0 ]; then
    echo "Ошибка: Не удалось запустить демон."
    exit 1
fi

echo "Операция завершена успешно."

