# Библиотека безопасности

Шаг 2.6, [ADR-043](../../../docs/adr/0043-platform-security.md). Цель CMake - `psim::platform_security`; OpenSSL 3 выполняет криптографические операции, Asio/Beast - сетевые операции. Новых зависимостей не добавлено.

| Интерфейс | Назначение |
|---|---|
| `Secret`, `read_secret` | Защищённый файл `0400`, проверки fd, размер до 64 КиБ по умолчанию, очистка буфера, ошибки без пути/содержимого |
| `TlsOptions`, `tls_context`, `configure_hostname` | TLS 1.3 снаружи, TLS 1.2+ с AEAD внутри, CA/имя, mTLS и полный свежий CRL |
| `TlsReload` | Атомарная публикация нового контекста, сохранение прежнего при ошибке |
| `Peer`, `authenticated_peer` | Идентичность и SHA-256 только из завершённого проверенного mTLS-соединения |
| `make_csr` | CSR P-256 с ограниченным URI SAN для внешней PKI |
| `http_request` | HTTPS GET/POST, SNI и DNS/IP verification, один дедлайн с DNS, ограничения заголовков/тела, без redirects и повторов |
| `JwtVerifier` | RS256/ES256 через EVP; issuer/audience/тип/сроки; роли клиента, тенант и области |
| `JwksCache` | Настроенный HTTPS endpoint, ротация, ограниченный устаревший кэш и частота запросов неизвестного `kid` |
| `authenticate_service` | mTLS URI SAN, разрешённый сервис и JWT; привязка `azp` для client credentials |
| `ConnectorTrust` | Регистрация отпечатка, перекрытие двух сертификатов, закрытие сессий при отзыве/истечении, восстановление |

## Ограничения и владение

JWT не больше 16 КиБ; header до 2 КиБ, claims до 12 КиБ, глубина JSON до 32. JWKS до 256 КиБ и 32 ключей, без повторяющихся `kid` и закрытого материала; RSA 2048-8192 бит либо P-256. Дублирующиеся JSON-поля, `none`, HMAC, неподдерживаемые `crit`, `jku/jwk/x5u`, некорректные base64url и ID tokens отклоняются. Access token имеет `typ=Bearer`, `iat` и `exp`, срок до 300 с, допуск часов до 30 с. Пустые роли и области не дают разрешений на действия.

`JwksCache` и `ConnectorTrust` принадлежат одному executor; использовать экземпляр на шард. `TlsReload` публикует snapshots между потоками; опубликованный контекст больше не изменять. Создание контекстов, CSR и чтение файлов - управляющие операции, не горячий путь обработки событий. HTTPS ограничивает тело до 1 МиБ по умолчанию, заголовки до 16 КиБ; общий дедлайн - 5 с. Публикация нового контекста применяется к новым соединениям, существующие держат прежний до закрытия.

Реестр по умолчанию ограничен 10 000 коннекторами и сессиями, двумя активными сертификатами на коннектор и 128 неистёкшими отпечатками в сумме с отзывами. Отзыв закрывает потоки через callbacks после удаления записей сессий, поэтому callback может безопасно вызвать `detach`. Callback не должен бросать исключения. Проверять `authenticate` на каждом сообщении, регулярно вызывать `expire`. Resource Catalog должен авторизовать регистрацию и восстановление, хранить записи и распространять изменения с подтверждением на каждый шлюз; эти сервисные функции относятся к шагу 4.2.

## Использование

```cpp
using namespace psim::platform;
security::JwksOptions options{
    .jwt = {.issuer = "https://identity.example/realms/psim", .audience = "psim"},
    .url = "https://identity.example/realms/psim/protocol/openid-connect/certs"};
auto cache = security::JwksCache::create(options);
if (!cache) {
  co_return std::unexpected(cache.error());
}
// Cache remains alive on its shard; verify() is awaited on that executor.
auto principal = co_await (*cache)->verify(access_token);
if (!principal) {
  co_return std::unexpected(principal.error());
}
// The owning service now checks the action and access scope.
```

Промежуточные HTTP/JWT/OpenSSL ошибки не включают токен, тело, пароль и путь к ключу. `Secret` нельзя форматировать или неявно преобразовать в строку; `view` предоставляет явный доступ на срок жизни владельца. Секреты в памяти сторонних драйверов не получают гарантию очистки от этого класса.

## Адаптеры и проверки

Kafka использует внутренний TLS-профиль и требует `ClientOptions::security` с CA и защищённой парой сертификат/ключ либо SCRAM-SHA-512 `username/password_file`. Изменение native TLS/SASL параметров через `properties` запрещено. Valkey использует общий TLS-профиль и `crl_file`. PostgreSQL проверяет `verify-full` и по умолчанию требует TLS 1.3; минимальный протокол можно настроить только на TLS 1.2 или TLS 1.3. Schema Registry использует HTTPS; OTLP использует TLS по умолчанию и CA из `telemetry.otlp_ca_file`.

```sh
task test:security
task test:security:sanitizers
task test:component
```

Профиль `security` запускает отдельные Kafka/PostgreSQL с mTLS и `valkey-tls`, без опубликованных портов. Тесты подтверждают реальные токены Keycloak, загрузку JWKS через HTTPS, Schema Registry/mTLS, публикацию и чтение Kafka, PostgreSQL и Valkey, продление и отзыв активного потока. Тестовые ключи находятся в игнорируемом `.cache/`, генерация использует только тестовый УЦ. Обычные наборы данных локального окружения сохраняются.

Явные исключения для изолированного локального стенда: Kafka/HTTP `allow_plaintext=true`, PostgreSQL `sslmode=disable`, Redis `tls.enabled=false`, OTLP `telemetry.otlp_tls=false`. Эксплуатационные процедуры и внешняя step-ca описаны в [регламенте доверия](../../../docs/security/platform-trust.md). ClickHouse и gRPC/HTTP серверы предметных сервисов подключают механизмы на своих шагах; проверка защищённой поставки и её ACL относится к Ф9 и шагу 10.3.
