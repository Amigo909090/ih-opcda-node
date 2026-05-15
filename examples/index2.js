const opcda = require('./build/Release/opcda');

if (!opcda || !opcda.OPCDA) {
    console.error('Failed to load OPCDA module');
    process.exit(1);
}

console.log('OPCDA module loaded');

let client;
try {
    // Создаём экземпляр, передавая callback для всех событий
    client = new opcda.OPCDA((event) => {
        console.log('Event:', event.type, event.data);
        if (event.type === 'connect') {
            if (event.data.success) {
                console.log('✅ Connected to OPC server!');
                // После успешного подключения создаём группу, добавляем теги и подписываемся
                (async () => {
                    try {
                        // Создаём группу (синхронно)
                        client.createGroup('myGroup', 1000, 0.0);
                        console.log('Group created');

                        // Добавляем теги (синхронно)
                        const tags = [
                            'StringValue',
                            'BooleanValue',
                            'ShortIntValue',
                            'IntegerValue',
                            'DoubleValue',
                            'DateTimeValue'
                        ];
                        for (const tag of tags) {
                            client.addItem('myGroup', tag);
                            console.log(`Item added: ${tag}`);
                        }

                        // Подписываемся на изменения (синхронно)
                        // callback игнорируется, все dataChange приходят в основной callback
                        //client.subscribe('myGroup', () => {}, ['dataChange']);
                        client.subscribe('myGroup');
                        console.log('Subscribed to group events');

                        // Асинхронное чтение
                        const value = await client.read('StringValue');
                        console.log('Read StringValue:', value);

                        // Асинхронная запись
                        await client.write('IntegerValue', 12345);
                        console.log('Write to IntegerValue OK');

                        // Чтение после записи
                        const newValue = await client.read('IntegerValue');
                        console.log('Read IntegerValue after write:', newValue);
                    } catch (err) {
                        console.error('Error during setup:', err);
                    }
                })();
            } else {
                console.error('❌ Connection failed:', event.data.error);
            }
        } else if (event.type === 'disconnect') {
            console.log('Disconnected from server');
        } else if (event.type === 'dataChange') {
            // event.data содержит объект с именами тегов и их значениями
            console.log('Data change:', event.data);
        }
    });
    console.log('OPCDA instance created');
} catch (err) {
    console.error('Failed to create OPCDA instance:', err);
    process.exit(1);
}

// Подключаемся к серверу
console.log('Connecting to localhost with progId opcserversim.Instance.1...');
try {
    client.connect('localhost', 'opcserversim.Instance.1');
} catch (err) {
    console.error('Connect call failed:', err);
}

// Обработка выхода
process.on('SIGINT', () => {
    console.log('Shutting down...');
    try {
        client.disconnect();
    } catch (err) {
        console.error('Error during disconnect:', err);
    }
    process.exit(0);
});

console.log('OPC DA client started');