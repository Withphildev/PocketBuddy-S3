FROM node:22-alpine
WORKDIR /app
COPY package.json ./
COPY server ./server
USER node
EXPOSE 5810
CMD ["node", "server/index.mjs"]
