const opcda = require('../build/Release/opcda');

if (!opcda || !opcda.OPCDA) {
    console.error('Failed to load OPCDA module');
    process.exit(1);
}

console.log('OPCDA module loaded');

let client;
try {
    client = new opcda.OPCDA((event) => {
        console.log('Event:', event.type, event.data);
        if (event.type === 'connect') {
            if (event.data.success) {
                console.log('✅ Connected to OPC server!');
                (async () => {
                    try {
                        // Первичный просмотр (browse)
                        const browseResult = await client.browse('');
                        console.log('Browse result (first 20 items):');
                        for (const item of browseResult.slice(0,20)) {
                            console.log(`${item.name}: type=${item.type}`);
                        }

                        // Получаем список всех тегов
                        const items = await client.browse('');
                        console.log(`Found ${items.length} items. Adding all to group...`);

                        // Создаём группу
                        client.createGroup('myGroup', 1000, 0.0);
                        console.log('Group created');

                        // Добавляем ВСЕ теги
                        for (const tag of items) {
                            client.addItem('myGroup', tag.name);
                            console.log(`Item added: ${tag.name}`);
                        }

                        // Подписываемся на изменения
                        client.subscribe('myGroup');
                        console.log('Subscribed to group events');

                        // Пример чтения
                        const readResult = await client.read('StringValue');
                        console.log('Read StringValue:', readResult);

                        
                        // Запись в TimeValue (строковое значение времени)
                        await client.write('BooleanValue', '0');
                        console.log('Write to BooleanValue OK');

                        // Проверка записи
                        const newValue = await client.read('BooleanValue');
                        console.log('Read BooleanValue after write:', newValue);
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
            console.log('Data change received:');
            for (const [tag, info] of Object.entries(event.data)) {
                const date = new Date(info.timestamp);
                console.log(`  ${tag}: value=${info.value}, quality=${info.quality}, timestamp=${date.toISOString()}`);
            }
        }
    });
    console.log('OPCDA instance created');
} catch (err) {
    console.error('Failed to create OPCDA instance:', err);
    process.exit(1);
}

// Подключение к серверу
console.log('Connecting to localhost with progId opcserversim.Instance.1...');
try {
    //client.connect('localhost', 'opcserversim.Instance.1');
    client.connect('localhost', '{CAE8D0E1-117B-11D5-924B-11C0F023E91C}');
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