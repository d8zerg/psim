workspace "PSIM Platform" "C4-модель MVP. Шаг плана 0.3." {

    !identifiers hierarchical

    model {
        properties {
            "structurizr.groupSeparator" "/"
        }

        operator = person "Оператор / старший смены" "Обрабатывает инциденты в консоли"
        admin = person "Администратор" "Настраивает каталог, правила, сценарии, доступ"
        integrator = person "Интегратор" "Разрабатывает и подключает коннекторы"
        auditor = person "Аудитор / руководитель" "Аудит и отчёты"
        opsEngineer = person "Инженер эксплуатации" "Дашборды, алерты, CLI"

        securitySystems = softwareSystem "Системы безопасности и инженерные системы" "Охрана, пожар, СКУД, видео, периметр, IoT" "External"
        keycloak = softwareSystem "Keycloak" "Identity Provider (OIDC)" "External"
        smtp = softwareSystem "Почтовый сервер" "SMTP" "External"
        webhookReceivers = softwareSystem "Получатели webhook" "Внешние системы" "External"
        observability = softwareSystem "Стек наблюдаемости" "OTel Collector, Prometheus, Grafana, логи, трассировки" "External"

        psim = softwareSystem "PSIM Platform" "Вендор-нейтральная платформа управления ситуациями" {

            connectorSdk = container "Connector SDK" "Библиотека коннекторов: протокол, буфер на диске, команды" "C++ library" "Library"

            group "Integration" {
                connectorGateway = container "Connector Gateway" "Сессии коннекторов, mTLS, валидация, кредиты, запись в Kafka, доставка команд" "C++23, gRPC" {
                    grpcServer = component "gRPC Server" "TLS, mTLS, двунаправленные потоки" "gRPC"
                    sessionManager = component "Session Manager" "Модель состояний сессии, heartbeat, вытеснение" "C++"
                    ingestPipeline = component "Ingest Pipeline" "Валидация, кредиты, пакетная запись, подтверждение после acks=all" "C++"
                    catalogView = component "Catalog View" "Локальная копия коннекторов и источников" "C++"
                    commandDispatcher = component "Command Dispatcher" "Доставка команд в локальные сессии, запись результатов" "C++"
                }
            }

            group "Catalog" {
                resourceCatalog = container "Resource Catalog" "Локации, устройства, источники, коннекторы, импорт, планы" "C++23, REST"
            }

            group "Event Processing" {
                normalizer = container "Normalizer" "Отображение в таксономию, обогащение, дедупликация, DLQ" "C++23"
                eventHistory = container "Event History" "Значимые события, поиск" "C++23, REST"
                correlationEngine = container "Correlation Engine" "Правила, окна, состояние, сигналы; управление правилами" "C++23, RocksDB" {
                    partitionWorker = component "Partition Worker" "Пакет -> обработка -> транзакция Kafka" "C++"
                    ruleEvaluator = component "Rule Evaluator" "Доменная функция (state, event, rules, now) -> (state', signals)" "C++ domain"
                    stateStore = component "State Store" "RocksDB + changelog" "C++"
                    watermarkTracker = component "Watermark Tracker" "Водяные знаки и таймеры окон" "C++"
                    ruleSetLoader = component "RuleSet Loader" "Атомарная смена набора правил" "C++"
                    repartitioner = component "Repartitioner" "Перераспределение для правил шире зоны" "C++"
                    ruleManagementApi = component "Rule Management API" "CRUD, валидация, пробный прогон, активация" "REST"
                }
            }

            group "Incident Management" {
                incidentService = container "Incident Service" "Агрегат инцидента, группировка, назначение, хронология" "C++23, REST" {
                    signalConsumer = component "Signal Consumer" "inbox + агрегат + outbox в одной транзакции" "C++"
                    incidentAggregate = component "Incident Aggregate" "Инварианты I1-I10" "C++ domain"
                    groupingPolicy = component "Grouping Policy" "Присоединить или создать" "C++ domain"
                    incidentCommandApi = component "Command API" "REST-команды с ETag и идемпотентностью" "REST"
                    responseEventsConsumer = component "Response Events Consumer" "blocking_steps, хронология" "C++"
                    outboxRelay = component "Outbox Relay" "Публикация outbox" "C++"
                }
            }

            group "Response" {
                responseEngine = container "Response Engine" "Сценарии, запуски, SLA, таймеры, эскалации" "C++23, REST"
                commandService = container "Command Service" "Жизненный цикл команд, подтверждения, таймауты" "C++23, REST"
                notificationService = container "Notification Service" "In-app, email, webhook" "C++23"
            }

            group "Reporting" {
                projectionService = container "Projection Service" "Лента, состояние объектов, отчётные агрегаты" "C++23, REST"
            }

            group "Audit" {
                auditService = container "Audit Service" "Цепочка хэшей, контрольные точки, проверка, поиск" "C++23, REST"
            }

            apiGateway = container "API Gateway" "REST v1, WebSocket realtime, JWT, ABAC, лимиты" "C++23, Boost.Beast"
            webApp = container "Web Application" "Консоль оператора, администрирование, аудит, отчёты" "React 19, TypeScript" "Browser"
            adminCli = container "psimctl" "Диагностика, DLQ, перестроение проекций, проверка аудита" "C++23" "CLI"

            kafka = container "Kafka" "Центральная шина (KRaft)" "Apache Kafka" "Queue"
            schemaRegistry = container "Schema Registry" "Схемы Protobuf" "Confluent-совместимый API"
            postgres = container "PostgreSQL" "Схема на сервис" "PostgreSQL 16+" "Database"
            redis = container "Redis-совместимое хранилище" "Сессии, лимиты, горячие проекции" "Redis API" "Database"
        }

        # Люди
        operator -> psim.webApp "Работает в консоли" "HTTPS"
        admin -> psim.webApp "Администрирует" "HTTPS"
        auditor -> psim.webApp "Аудит и отчёты" "HTTPS"
        integrator -> psim.connectorSdk "Разрабатывает коннекторы"
        opsEngineer -> psim.adminCli "Диагностика и восстановление"
        opsEngineer -> observability "Дашборды и алерты"

        # Внешние системы
        securitySystems -> psim.connectorSdk "События, исполнение команд" "API производителя"
        psim.connectorSdk -> psim.connectorGateway "Протокол коннекторов v1" "gRPC, mTLS"
        psim.webApp -> psim.apiGateway "REST v1, WebSocket" "HTTPS, WSS"
        psim.webApp -> keycloak "Вход" "OIDC + PKCE"
        psim.apiGateway -> keycloak "JWKS" "HTTPS"
        psim.notificationService -> smtp "Email" "SMTP"
        psim.notificationService -> webhookReceivers "Webhook" "HTTPS + HMAC"
        psim -> observability "Трассировки, метрики, логи" "OTLP, Prometheus"

        # Потоки через Kafka
        psim.connectorGateway -> psim.kafka "psim.ingest.raw.v1, psim.commands.results.v1" "Kafka"
        psim.kafka -> psim.connectorGateway "psim.commands.v1, psim.catalog.v1" "Kafka"
        psim.kafka -> psim.normalizer "psim.ingest.raw.v1, psim.catalog.v1, psim.config.v1" "Kafka"
        psim.normalizer -> psim.kafka "psim.events.normalized.v1 (EOS)" "Kafka"
        psim.kafka -> psim.eventHistory "psim.events.normalized.v1" "Kafka"
        psim.kafka -> psim.correlationEngine "psim.events.normalized.v1, psim.config.v1" "Kafka"
        psim.correlationEngine -> psim.kafka "psim.signals.v1 (EOS), changelog, config" "Kafka"
        psim.kafka -> psim.incidentService "psim.signals.v1, psim.response.events.v1" "Kafka"
        psim.incidentService -> psim.kafka "psim.incidents.events.v1 (outbox)" "Kafka"
        psim.kafka -> psim.responseEngine "psim.incidents.events.v1" "Kafka"
        psim.responseEngine -> psim.kafka "psim.response.events.v1 (outbox)" "Kafka"
        psim.responseEngine -> psim.commandService "Команды шагов сценария" "REST"
        psim.commandService -> psim.kafka "psim.commands.v1, psim.response.events.v1 (outbox)" "Kafka"
        psim.kafka -> psim.commandService "psim.commands.results.v1" "Kafka"
        psim.kafka -> psim.notificationService "psim.response.events.v1" "Kafka"
        psim.kafka -> psim.projectionService "incidents, response, normalized (state), catalog" "Kafka"
        psim.kafka -> psim.apiGateway "incidents, response, catalog (realtime)" "Kafka"
        psim.kafka -> psim.auditService "psim.audit.v1" "Kafka"
        psim.resourceCatalog -> psim.kafka "psim.catalog.v1 (outbox)" "Kafka"

        # Синхронные вызовы
        psim.apiGateway -> psim.resourceCatalog "REST" "HTTPS, mTLS"
        psim.apiGateway -> psim.incidentService "REST" "HTTPS, mTLS"
        psim.apiGateway -> psim.responseEngine "REST" "HTTPS, mTLS"
        psim.apiGateway -> psim.commandService "REST" "HTTPS, mTLS"
        psim.apiGateway -> psim.correlationEngine "REST: правила" "HTTPS, mTLS"
        psim.apiGateway -> psim.eventHistory "REST: поиск событий" "HTTPS, mTLS"
        psim.apiGateway -> psim.projectionService "REST: снимки, отчёты" "HTTPS, mTLS"
        psim.apiGateway -> psim.auditService "REST: аудит" "HTTPS, mTLS"
        psim.adminCli -> psim.kafka "DLQ, проигрывание" "Kafka"
        psim.adminCli -> psim.apiGateway "Операции администрирования" "HTTPS"

        # Хранилища
        psim.connectorGateway -> psim.redis "Реестр сессий"
        psim.commandService -> psim.redis "Проверка сессии"
        psim.apiGateway -> psim.redis "Лимиты"
        psim.projectionService -> psim.redis "Горячие проекции"
        psim.resourceCatalog -> psim.postgres "catalog"
        psim.eventHistory -> psim.postgres "event_history"
        psim.correlationEngine -> psim.postgres "правила"
        psim.incidentService -> psim.postgres "incident"
        psim.responseEngine -> psim.postgres "response"
        psim.commandService -> psim.postgres "command"
        psim.notificationService -> psim.postgres "notification"
        psim.projectionService -> psim.postgres "projection"
        psim.auditService -> psim.postgres "audit"
        psim.normalizer -> psim.schemaRegistry "Схемы"
        psim.correlationEngine -> psim.schemaRegistry "Схемы"

        # Компоненты: Connector Gateway
        psim.connectorSdk -> psim.connectorGateway.grpcServer "Протокол коннекторов v1" "gRPC"
        psim.connectorGateway.grpcServer -> psim.connectorGateway.sessionManager "Открытие и закрытие сессий"
        psim.connectorGateway.grpcServer -> psim.connectorGateway.ingestPipeline "Пакеты событий"
        psim.connectorGateway.ingestPipeline -> psim.connectorGateway.catalogView "Принадлежность источников"
        psim.connectorGateway.ingestPipeline -> psim.kafka "psim.ingest.raw.v1"
        psim.connectorGateway.sessionManager -> psim.redis "Реестр сессий"
        psim.kafka -> psim.connectorGateway.commandDispatcher "psim.commands.v1"
        psim.connectorGateway.commandDispatcher -> psim.connectorGateway.sessionManager "Поиск локальной сессии"
        psim.kafka -> psim.connectorGateway.catalogView "psim.catalog.v1"

        # Компоненты: Correlation Engine
        psim.kafka -> psim.correlationEngine.partitionWorker "psim.events.normalized.v1"
        psim.correlationEngine.partitionWorker -> psim.correlationEngine.ruleEvaluator "Вычисление"
        psim.correlationEngine.partitionWorker -> psim.correlationEngine.stateStore "Чтение и запись состояния"
        psim.correlationEngine.partitionWorker -> psim.correlationEngine.watermarkTracker "Время событий"
        psim.correlationEngine.partitionWorker -> psim.correlationEngine.repartitioner "События правил шире зоны"
        psim.correlationEngine.partitionWorker -> psim.kafka "signals + offsets (txn)"
        psim.correlationEngine.stateStore -> psim.kafka "changelog (txn)"
        psim.kafka -> psim.correlationEngine.ruleSetLoader "psim.config.v1"
        psim.correlationEngine.ruleSetLoader -> psim.correlationEngine.partitionWorker "Активный набор"
        psim.correlationEngine.repartitioner -> psim.kafka "psim.correlation.repartition.v1"
        psim.apiGateway -> psim.correlationEngine.ruleManagementApi "REST"
        psim.correlationEngine.ruleManagementApi -> psim.postgres "Правила"
        psim.correlationEngine.ruleManagementApi -> psim.kafka "psim.config.v1"

        # Компоненты: Incident Service
        psim.kafka -> psim.incidentService.signalConsumer "psim.signals.v1"
        psim.incidentService.signalConsumer -> psim.incidentService.groupingPolicy "Выбор инцидента"
        psim.incidentService.signalConsumer -> psim.incidentService.incidentAggregate "IngestSignal"
        psim.incidentService.signalConsumer -> psim.postgres "inbox + агрегат + outbox"
        psim.apiGateway -> psim.incidentService.incidentCommandApi "REST"
        psim.incidentService.incidentCommandApi -> psim.incidentService.incidentAggregate "Команды пользователей"
        psim.incidentService.incidentCommandApi -> psim.postgres "агрегат + outbox"
        psim.kafka -> psim.incidentService.responseEventsConsumer "psim.response.events.v1"
        psim.incidentService.responseEventsConsumer -> psim.incidentService.incidentAggregate "UpdateBlockingSteps"
        psim.incidentService.outboxRelay -> psim.postgres "Чтение outbox"
        psim.incidentService.outboxRelay -> psim.kafka "psim.incidents.events.v1, psim.audit.v1"

        # Развёртывание
        single = deploymentEnvironment "Single node" {
            deploymentNode "Сервер" "Debian 12 / Astra Linux 1.8" "Docker Compose или systemd" {
                containerInstance psim.connectorGateway
                containerInstance psim.resourceCatalog
                containerInstance psim.normalizer
                containerInstance psim.eventHistory
                containerInstance psim.correlationEngine
                containerInstance psim.incidentService
                containerInstance psim.responseEngine
                containerInstance psim.commandService
                containerInstance psim.notificationService
                containerInstance psim.projectionService
                containerInstance psim.auditService
                containerInstance psim.apiGateway
                containerInstance psim.kafka
                containerInstance psim.schemaRegistry
                containerInstance psim.postgres
                containerInstance psim.redis
            }
        }

        cluster = deploymentEnvironment "VM cluster" {
            deploymentNode "Балансировщики" "HAProxy + keepalived" "" 2
            deploymentNode "infra" "Debian 12 / Astra Linux 1.8" "VM 16 vCPU, 64 ГБ, NVMe" 3 {
                containerInstance psim.kafka
                containerInstance psim.postgres
                containerInstance psim.redis
                containerInstance psim.schemaRegistry
            }
            deploymentNode "app" "Debian 12 / Astra Linux 1.8" "VM 16 vCPU, 32 ГБ" 2 {
                containerInstance psim.connectorGateway
                containerInstance psim.resourceCatalog
                containerInstance psim.normalizer
                containerInstance psim.eventHistory
                containerInstance psim.correlationEngine
                containerInstance psim.incidentService
                containerInstance psim.responseEngine
                containerInstance psim.commandService
                containerInstance psim.notificationService
                containerInstance psim.projectionService
                containerInstance psim.auditService
                containerInstance psim.apiGateway
            }
        }
    }

    views {
        systemContext psim "L1-Context" {
            include *
            autolayout lr
        }

        container psim "L2-Containers" {
            include *
            autolayout lr
        }

        component psim.connectorGateway "L3-ConnectorGateway" {
            include *
            autolayout lr
        }

        component psim.correlationEngine "L3-CorrelationEngine" {
            include *
            autolayout lr
        }

        component psim.incidentService "L3-IncidentService" {
            include *
            autolayout lr
        }

        deployment psim single "Deploy-SingleNode" {
            include *
            autolayout lr
        }

        deployment psim cluster "Deploy-VMCluster" {
            include *
            autolayout lr
        }

        styles {
            element "Person" {
                shape Person
                background #1f6feb
                color #ffffff
            }
            element "Software System" {
                background #1f6feb
                color #ffffff
            }
            element "External" {
                background #8b949e
                color #ffffff
            }
            element "Container" {
                background #2f81f7
                color #ffffff
            }
            element "Component" {
                background #79c0ff
                color #0d1117
            }
            element "Database" {
                shape Cylinder
            }
            element "Queue" {
                shape Pipe
            }
            element "Browser" {
                shape WebBrowser
            }
            element "Library" {
                shape Component
            }
            element "CLI" {
                shape Terminal
            }
        }
    }
}
