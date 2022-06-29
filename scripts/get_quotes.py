import json
import requests
import sys


URL='https://api.quotable.io/random?tags=technology|success|business|inspirational|education|future|science'
QUOTE_LIST = []

def main(num_quotes=100):
    for _ in range(int(num_quotes)):
        r = requests.get(URL, headers={'Content-Type': 'application/json'})
        quote = r.json()
        QUOTE_LIST.append({
            'author': quote['author'],
            'content': quote['content']
        })

    with open('quotes.json', 'w') as f:
        f.write(json.dumps(QUOTE_LIST, indent=4))


if __name__ == '__main__':
    main(num_quotes=sys.argv[1])
